/**
 ******************************************************************************
 * @file    Http_client.c
 * @brief   HTTP 分块上传客户端实现
 *
 *  上传流程:
 *    1. 加互斥锁 (防止多任务并发上传)
 *    2. 打开图片文件, 获取总大小
 *    3. 加载断点续传状态 (IMG_xxx.ST), 若文件大小变化则重置
 *    4. ESP01 建立 TCP 连接
 *    5. 发送 HTTP 请求头 (含 Content-Length, X-Chunk-Offset 等)
 *    6. 循环分块读取文件并发送 (每块 HTTP_CHUNK_SIZE 字节)
 *    7. 每发送一块保存一次状态 (支持断电续传)
 *    8. 全部发送完成后删除状态文件
 *    9. 关闭文件, 释放互斥锁
 *
 *  注意:
 *    - FIL 对象较大 (FF_MAX_SS=4096), 不能放在任务栈中, 用静态全局变量
 *    - 发送失败时保存当前偏移, 下次调用从断点继续
 *    - 服务器需支持 X-Chunk-Offset 头来处理断点续传
 ******************************************************************************
 */
#define LOG_TAG "http"

#include "Http_client.h"
#include "esp01.h"
#include "ff.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "elog.h"
#include <stdio.h>

/* HTTP 上传互斥锁 (防止多任务并发上传) */
static SemaphoreHandle_t s_http_mutex = NULL;

/* 分块发送缓冲区 */
static uint8_t s_chunk[HTTP_CHUNK_SIZE];

/* FIL 对象较大, 用静态全局变量避免栈溢出 */
static FIL s_image_file;
static FIL s_state_file;

/*===========================================================================
 * state_path
 *   生成状态文件路径: 0:IMG_xxx.ST
 *===========================================================================*/
static void state_path(char *path, uint16_t size, uint32_t id)
{
    snprintf(path, size, "0:IMG_%03lu.ST", (unsigned long)id);
}

/*===========================================================================
 * state_load
 *   从 .ST 文件加载断点续传状态
 *   返回: 0=成功, 1=失败 (文件不存在/校验错误)
 *===========================================================================*/
static uint8_t state_load(uint32_t id, HttpUploadState_t *state)
{
    char path[32];
    UINT read = 0;
    FRESULT result;

    state_path(path, sizeof(path), id);
    result = f_open(&s_state_file, path, FA_READ);
    if(result != FR_OK)
        return 1;

    result = f_read(&s_state_file, state, sizeof(*state), &read);
    (void)f_close(&s_state_file);

    if(result != FR_OK || read != sizeof(*state))
        return 1;

    /* 校验魔数 */
    return (state->magic == HTTP_STATE_MAGIC) ? 0 : 1;
}

/*===========================================================================
 * state_save
 *   保存断点续传状态到 .ST 文件
 *   返回: 0=成功, 1=失败
 *===========================================================================*/
static uint8_t state_save(uint32_t id, const HttpUploadState_t *state)
{
    char path[32];
    UINT written = 0;
    FRESULT result;

    state_path(path, sizeof(path), id);
    result = f_open(&s_state_file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if(result != FR_OK)
        return 1;

    result = f_write(&s_state_file, state, sizeof(*state), &written);
    (void)f_close(&s_state_file);

    return (result == FR_OK && written == sizeof(*state)) ? 0 : 1;
}

/*===========================================================================
 * HTTP_ClearState
 *   删除指定图片的断点续传状态文件
 *===========================================================================*/
void HTTP_ClearState(uint32_t id)
{
    char path[32];

    state_path(path, sizeof(path), id);
    (void)f_unlink(path);
}

/*===========================================================================
 * HTTP_UploadFile
 *   上传图片文件到 HTTP 服务器 (支持断点续传)
 *===========================================================================*/
uint8_t HTTP_UploadFile(const char *path, uint32_t img_id)
{
    UINT read = 0;
    FRESULT file_result;
    HttpUploadState_t state;
    char header[320];
    uint32_t total;
    uint16_t header_len;
    uint8_t return_code = HTTP_OK;
    uint8_t file_opened = 0;

    /* 参数检查 */
    if(path == NULL)
        return HTTP_ERR_FILE;

    /* 懒创建互斥锁 */
    if(s_http_mutex == NULL)
        s_http_mutex = xSemaphoreCreateMutex();

    /* 获取互斥锁 (超时 10s) */
    if(s_http_mutex == NULL ||
       xSemaphoreTake(s_http_mutex, pdMS_TO_TICKS(10000)) != pdTRUE)
    {
        log_e("HTTP mutex timeout");
        return HTTP_ERR_CONNECT;
    }

    /*--- 打开图片文件 ---*/
    file_result = f_open(&s_image_file, path, FA_READ);
    if(file_result != FR_OK)
    {
        log_e("open upload file failed: %d, %s", file_result, path);
        return_code = HTTP_ERR_FILE;
        goto cleanup;
    }
    file_opened = 1;
    total = (uint32_t)f_size(&s_image_file);

    /*--- 加载断点续传状态 ---*/
    /* 如果状态文件不存在, 或文件大小变化 (重新生成了图片), 则重置状态 */
    if(state_load(img_id, &state) != 0 || state.total_size != total)
    {
        state.total_size  = total;
        state.sent_offset = 0;
        state.retry_count = 0;
        state.magic       = HTTP_STATE_MAGIC;
    }

    /* 偏移越界保护 */
    if(state.sent_offset > total)
        state.sent_offset = 0;

    log_i("upload start: %s, size=%lu, offset=%lu",
          path, (unsigned long)total, (unsigned long)state.sent_offset);

    /*--- 建立 TCP 连接 ---*/
    if(!ESP01_ConnectTCP(HTTP_SERVER_IP, HTTP_SERVER_PORT, 8000))
    {
        log_e("HTTP server connect failed: %s:%u",
              HTTP_SERVER_IP, HTTP_SERVER_PORT);
        return_code = HTTP_ERR_CONNECT;
        goto cleanup;
    }

    /*--- 构造并发送 HTTP 请求头 ---*/
    header_len = (uint16_t)snprintf(
        header, sizeof(header),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %lu\r\n"
        "X-Device-Id: %s\r\n"
        "X-Img-Id: %lu\r\n"
        "X-Chunk-Offset: %lu\r\n"
        "X-Total-Size: %lu\r\n"
        "Connection: close\r\n\r\n",
        HTTP_UPLOAD_PATH,
        HTTP_SERVER_IP,
        HTTP_SERVER_PORT,
        (unsigned long)(total - state.sent_offset),
        HTTP_DEV_ID,
        (unsigned long)img_id,
        (unsigned long)state.sent_offset,
        (unsigned long)total);

    if(header_len >= sizeof(header) ||
       !Usart2_SendBinary((uint8_t *)header, header_len, 8000))
    {
        log_e("HTTP header send failed");
        return_code = HTTP_ERR_SEND;
        goto cleanup;
    }

    /*--- 分块发送图片数据 ---*/
    while(state.sent_offset < total)
    {
        uint32_t remain = total - state.sent_offset;
        uint16_t count = (uint16_t)((remain > HTTP_CHUNK_SIZE) ?
                                    HTTP_CHUNK_SIZE : remain);

        /* 定位到当前偏移并读取一块数据 */
        file_result = f_lseek(&s_image_file, state.sent_offset);
        if(file_result == FR_OK)
            file_result = f_read(&s_image_file, s_chunk, count, &read);

        /* 读取或发送失败: 保存状态, 返回错误 (下次可续传) */
        if(file_result != FR_OK || read != count ||
           !Usart2_SendBinary(s_chunk, count, 8000))
        {
            log_e("HTTP body send failed at %lu/%lu, fr=%d, read=%u",
                  (unsigned long)state.sent_offset,
                  (unsigned long)total,
                  file_result, read);
            state.retry_count++;
            (void)state_save(img_id, &state);
            return_code = HTTP_ERR_SEND;
            goto cleanup;
        }

        /* 更新偏移并保存状态 (每块都保存, 支持断电续传) */
        state.sent_offset += count;
        (void)state_save(img_id, &state);
    }

    /*--- 全部发送完成, 删除状态文件 ---*/
    HTTP_ClearState(img_id);
    log_i("upload data sent: %s, %lu bytes",
          path, (unsigned long)total);

cleanup:
    /* 关闭文件 */
    if(file_opened)
        (void)f_close(&s_image_file);

    /* 释放互斥锁 */
    xSemaphoreGive(s_http_mutex);
    return return_code;
}

/*===========================================================================
 * HTTP_RetryPending
 *   扫描根目录下所有 .ST 状态文件, 重试未完成的上传
 *   通常在系统启动时调用
 *===========================================================================*/
void HTTP_RetryPending(void)
{
    DIR dir;
    FILINFO info;
    uint32_t id;
    char image[32];

    /* 打开根目录 */
    if(f_opendir(&dir, "0:/") != FR_OK)
        return;

    /* 遍历目录项 */
    while(f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0)
    {
        /* 匹配 IMG_xxx.ST 状态文件 */
        if(sscanf(info.fname, "IMG_%lu.ST", (unsigned long *)&id) != 1)
            continue;

        /* 构造对应图片路径并重试上传 */
        snprintf(image, sizeof(image), "0:IMG_%03lu.JPG", (unsigned long)id);
        (void)HTTP_UploadFile(image, id);
    }

    (void)f_closedir(&dir);
}
