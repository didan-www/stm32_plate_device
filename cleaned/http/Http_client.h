/**
 ******************************************************************************
 * @file    Http_client.h
 * @brief   HTTP 分块上传客户端头文件
 *
 *  功能:
 *    - 通过 ESP01 透传模式, 将 W25Q64 中的 JPEG 图片分块上传到 HTTP 服务器
 *    - 支持断点续传 (状态保存在 IMG_xxx.ST 文件中)
 *    - 支持失败重试 (HTTP_RetryPending)
 *
 *  上传协议:
 *    POST /api/v1/plate/upload HTTP/1.1
 *    Host: <ip>:<port>
 *    Content-Type: application/octet-stream
 *    Content-Length: <剩余字节数>
 *    X-Device-Id: <设备ID>
 *    X-Img-Id: <图片ID>
 *    X-Chunk-Offset: <已发送偏移>
 *    X-Total-Size: <总大小>
 *    Connection: close
 *
 *    <二进制图片数据>
 ******************************************************************************
 */
#ifndef __HTTP_CLIENT_H__
#define __HTTP_CLIENT_H__

#include "stm32f4xx.h"
#include <stdint.h>

/* HTTP 服务器配置 */
#define HTTP_SERVER_IP      "192.168.1.9"
#define HTTP_SERVER_PORT    8089
#define HTTP_UPLOAD_PATH    "/api/v1/plate/upload"
#define HTTP_DEV_ID         "MaLi-001"

/* 单次分块发送大小 (不超过 ESP01 缓冲区能力) */
#define HTTP_CHUNK_SIZE     1024U

/* 单次操作超时重试次数 */
#define HTTP_CHUNK_RETRY    3U

/* 上传结果码 */
#define HTTP_OK             0U   /* 上传成功 */
#define HTTP_ERR_CONNECT    1U   /* TCP 连接失败 */
#define HTTP_ERR_SEND       2U   /* 发送失败 */
#define HTTP_ERR_RESP       3U   /* 响应异常 */
#define HTTP_ERR_FILE       4U   /* 文件操作失败 */
#define HTTP_ERR_ABORT      5U   /* 上传中止 */

/**
 * @brief  断点续传状态 (保存在 IMG_xxx.ST 文件中)
 */
typedef struct
{
    uint32_t total_size;     /* 文件总大小 (字节) */
    uint32_t sent_offset;    /* 已发送偏移 (字节) */
    uint16_t retry_count;    /* 重试次数 */
    uint16_t magic;          /* 魔数校验 (HTTP_STATE_MAGIC) */
} HttpUploadState_t;

/* 状态文件魔数 */
#define HTTP_STATE_MAGIC    0xA55AU

/**
 * @brief  上传指定图片文件到 HTTP 服务器
 * @param  path    文件路径 (如 "0:IMG_001.JPG")
 * @param  img_id  图片 ID (用于状态文件命名和 X-Img-Id)
 * @return 上传结果码 (HTTP_OK / HTTP_ERR_xxx)
 * @note   支持断点续传, 失败后状态自动保存, 下次调用继续上传
 */
uint8_t HTTP_UploadFile(const char *path, uint32_t img_id);

/**
 * @brief  扫描根目录下所有 .ST 状态文件, 重试未完成的上传
 * @note   通常在系统启动时调用, 恢复上次断电前未完成的上传
 */
void HTTP_RetryPending(void);

/**
 * @brief  删除指定图片的断点续传状态文件
 * @param  img_id  图片 ID
 */
void HTTP_ClearState(uint32_t img_id);

#endif /* __HTTP_CLIENT_H__ */
