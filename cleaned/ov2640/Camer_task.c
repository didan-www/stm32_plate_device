/**
 ******************************************************************************
 * @file    Camer_task.c
 * @brief   OV2640 JPEG 采集任务
 *
 *  采集架构:
 *    - DMA 双缓冲: s_dma_buf0 / s_dma_buf1, 各 1024 word (4KB)
 *    - DMA TC 中断: 每填满一个 4KB 缓冲区触发, 拷贝到汇总缓冲
 *    - DCMI FRAME 中断: 一帧结束触发, 处理帧尾剩余数据
 *    - 汇总缓冲: s_jpeg_data_buf, 10240 word (40KB), 存放完整 JPEG
 *
 *  采集流程 (每次拍照):
 *    1. DCMI_Stop -> My_DCMI_Init -> DCMI_DMA_Init -> DCMI_Start
 *    2. warmup 2 帧 (丢弃前两帧, 让 AEC/AGC/AWB 稳定)
 *    3. 采集正式帧, 等待帧完成信号量
 *    4. 查找 JPEG SOI(FFD8) / EOI(FFD9)
 *    5. 保存到 W25Q64 Flash, 发送事件
 *    6. 失败重试最多 3 次
 *
 *  重要: DMA 缓冲区用 section("DMA_SRAM") 定位到普通 SRAM (0x2000xxxx),
 *       不能放在 CCM (0x1000xxxx), DMA2 无法访问 CCM.
 ******************************************************************************
 */
#define LOG_TAG "camera"

#include "stm32f4xx.h"
#include "Camer_task.h"
#include "Ov2640.h"
#include "My_dcmi.h"
#include "w25q64.h"
#include "ff.h"
#include "app.h"
#include "elog.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include <string.h>
#include <stdio.h>

/*===========================================================================
 * 缓冲区定义
 *   单位: uint32_t (word), 实际字节数 = 元素数 * 4
 *   DMA 双缓冲: 2 * 4KB = 8KB
 *   JPEG 汇总缓冲: 40KB
 *===========================================================================*/
#define JPEG_DMA_BUF_WORDS    (1  * 1024)   /* DMA 单缓冲 4KB */
#define JPEG_DATA_BUF_WORDS   (10 * 1024)   /* JPEG 汇总缓冲 40KB */

/* DMA2 只能访问普通 SRAM, 不能访问 CCM. 通过独立段配合 scatter 文件固定到 SRAM. */
static uint32_t __attribute__((section("DMA_SRAM"), aligned(4)))
    s_dma_buf0[JPEG_DMA_BUF_WORDS];
static uint32_t __attribute__((section("DMA_SRAM"), aligned(4)))
    s_dma_buf1[JPEG_DMA_BUF_WORDS];
static uint32_t __attribute__((section("DMA_SRAM"), aligned(4)))
    s_jpeg_data_buf[JPEG_DATA_BUF_WORDS];

/*===========================================================================
 * 采集状态变量
 *   s_jpeg_data_len  : 已采集的 word 数
 *   s_jpeg_data_ok   : 0=采集中, 1=帧完成待处理, 2=通知中断重启DMA
 *   s_jpeg_overflow  : 1=汇总缓冲区溢出
 *===========================================================================*/
static volatile uint32_t s_jpeg_data_len = 0;
static volatile uint8_t  s_jpeg_data_ok  = 0;
static volatile uint8_t  s_jpeg_overflow = 0;

/* 任务句柄 */
TaskHandle_t Camera_Task_Handle = NULL;

/* 帧完成信号量: DCMI 帧中断中 Give, 任务中 Take */
static SemaphoreHandle_t xFrameDoneSem = NULL;

/* 拍照请求队列: SRF05 任务等通过 Camera_TriggerCapture 发送请求 */
static QueueHandle_t xCaptureQueue = NULL;

/* 事件队列 (在 app.c 中创建) */
extern QueueHandle_t Event_Queue;

/*===========================================================================
 * jpeg_data_process
 *   DCMI 帧中断中调用
 *
 *   状态机:
 *     s_jpeg_data_ok == 0: 当前帧正在采集, 帧中断到来表示一帧结束
 *                          -> 停止DMA, 读取帧尾剩余数据, 置 ok=1, 通知任务
 *     s_jpeg_data_ok == 2: 任务已处理完上一帧, 通知中断重启DMA开始下一帧
 *                          -> 重置NDTR, 开DMA, 置 ok=0
 *===========================================================================*/
void jpeg_data_process(void)
{
    uint16_t  rlen, i;
    uint32_t *pbuf;

    if(s_jpeg_data_ok == 0)         /* 当前帧采集中, 这是帧结束中断 */
    {
        /* 停止 DMA, 防止继续写入 */
        DMA_Cmd(DMA2_Stream1, DISABLE);
        uint32_t guard = 10000;
        while(DMA_GetCmdStatus(DMA2_Stream1) != DISABLE && guard--);

        /* 计算当前缓冲区中已写入但未处理的 word 数 */
        rlen = (uint16_t)(JPEG_DMA_BUF_WORDS - DMA_GetCurrDataCounter(DMA2_Stream1));
        if(rlen > JPEG_DMA_BUF_WORDS) rlen = JPEG_DMA_BUF_WORDS;

        /* 检查汇总缓冲区是否溢出 */
        if(s_jpeg_data_len + rlen > JPEG_DATA_BUF_WORDS)
        {
            BaseType_t woken = pdFALSE;
            s_jpeg_overflow = 1;
            s_jpeg_data_ok = 1;
            DCMI_Stop();
            if(xFrameDoneSem != NULL)
                xSemaphoreGiveFromISR(xFrameDoneSem, &woken);
            portYIELD_FROM_ISR(woken);
            return;
        }

        /* 拷贝帧尾数据到汇总缓冲 */
        pbuf = s_jpeg_data_buf + s_jpeg_data_len;

        /* CT位(bit19)=1 表示 DMA 当前正在写 M1, 则未写完的是 M1; 否则是 M0 */
        if(DMA2_Stream1->CR & (1 << 19))
        {
            for(i = 0; i < rlen; i++) pbuf[i] = s_dma_buf1[i];
        }
        else
        {
            for(i = 0; i < rlen; i++) pbuf[i] = s_dma_buf0[i];
        }

        s_jpeg_data_len += rlen;
        s_jpeg_data_ok   = 1;

        /* 通知任务: 一帧采集完成 */
        {
            BaseType_t woken = pdFALSE;
            if(xFrameDoneSem != NULL)
                xSemaphoreGiveFromISR(xFrameDoneSem, &woken);
            portYIELD_FROM_ISR(woken);
        }
    }

    if(s_jpeg_data_ok == 2)         /* 任务已处理完, 通知重启 DMA 开始下一帧 */
    {
        DMA_SetCurrDataCounter(DMA2_Stream1, JPEG_DMA_BUF_WORDS);
        DMA_Cmd(DMA2_Stream1, ENABLE);
        s_jpeg_data_ok  = 0;
        s_jpeg_data_len = 0;
    }
}

/*===========================================================================
 * jpeg_dcmi_rx_callback
 *   DMA TC 中断中调用, 每填满一个 4KB 缓冲区触发一次
 *   将刚填满的缓冲区拷贝到汇总缓冲
 *===========================================================================*/
static void jpeg_dcmi_rx_callback(void)
{
    uint32_t *pbuf;
    uint16_t  i;

    /* 上一帧未处理完则丢弃 (warmup 帧或溢出保护) */
    if(s_jpeg_data_ok != 0) return;

    /* 检查汇总缓冲区是否溢出 */
    if(s_jpeg_data_len + JPEG_DMA_BUF_WORDS > JPEG_DATA_BUF_WORDS)
    {
        s_jpeg_overflow = 1;
        DCMI_Stop();
        s_jpeg_data_ok = 1;
        if(xFrameDoneSem != NULL)
        {
            BaseType_t woken = pdFALSE;
            xSemaphoreGiveFromISR(xFrameDoneSem, &woken);
            portYIELD_FROM_ISR(woken);
        }
        return;
    }

    pbuf = s_jpeg_data_buf + s_jpeg_data_len;

    /* CT位(bit19)=1 表示 DMA 当前正在写 M1, 则刚填满的是 M0; 反之亦然 */
    if(DMA2_Stream1->CR & (1 << 19))
    {
        for(i = 0; i < JPEG_DMA_BUF_WORDS; i++) pbuf[i] = s_dma_buf0[i];
    }
    else
    {
        for(i = 0; i < JPEG_DMA_BUF_WORDS; i++) pbuf[i] = s_dma_buf1[i];
    }

    s_jpeg_data_len += JPEG_DMA_BUF_WORDS;

    /* 正常帧由 DCMI FRAME 中断结束, 此处不截断 */
}

/*===========================================================================
 * Camera_TriggerCapture
 *   外部触发拍照 (如 SRF05 检测到车辆)
 *===========================================================================*/
void Camera_TriggerCapture(uint8_t mode)
{
    if(xCaptureQueue == NULL) return;

    if(xQueueSend(xCaptureQueue, &mode, 0) != pdTRUE)
    {
        log_w("capture queue FULL");
        log_w("  waiting=%u, task state=%d",
              (unsigned)uxQueueMessagesWaiting(xCaptureQueue),
              (Camera_Task_Handle != NULL) ?
                  eTaskGetState(Camera_Task_Handle) : -1);
    }
    else
        log_d("capture queued");
}

/*===========================================================================
 * Camera_GetFrameBuf / Camera_GetFrameSize
 *   获取当前帧数据 (供上传等使用)
 *===========================================================================*/
uint8_t  *Camera_GetFrameBuf(void)  { return (uint8_t *)s_jpeg_data_buf; }
uint32_t  Camera_GetFrameSize(void) { return s_jpeg_data_len * 4; }

/*===========================================================================
 * save_jpeg_to_flash
 *   将 JPEG 数据保存到 W25Q64 (FatFs)
 *   文件名: IMG_xxx.JPG
 *===========================================================================*/
static uint8_t save_jpeg_to_flash(const uint8_t *buf, uint32_t size,
                                  uint32_t img_id)
{
    static FIL file;
    char    path[32];
    UINT    bw;
    FRESULT fr;

    snprintf(path, sizeof(path), "0:IMG_%03lu.JPG", (unsigned long)img_id);

    fr = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if(fr != FR_OK)
    {
        log_e("f_open %s failed: %d", path, fr);
        return 1;
    }

    fr = f_write(&file, buf, size, &bw);
    f_close(&file);

    if(fr != FR_OK || bw != size)
    {
        log_e("f_write failed: %d, %u/%lu", fr, bw, (unsigned long)size);
        return 1;
    }

    log_i("saved %s (%lu bytes)", path, (unsigned long)bw);
    return 0;
}

/*===========================================================================
 * Camera_Task
 *   摄像头主任务
 *
 *   初始化:
 *     1. 创建信号量和队列
 *     2. OV2640 初始化 (硬复位 + SCCB + 软复位 + 读ID + 写初始化表)
 *     3. 配置 JPEG 模式, 640x480
 *     4. 回读关键寄存器确认配置
 *     5. 检测 PCLK 是否正常输出
 *     6. DCMI 初始化
 *
 *   主循环:
 *     1. 等待拍照请求
 *     2. 最多重试 3 次采集
 *     3. 每次采集: 复位DCMI -> 配DMA -> 启动 -> warmup 2帧 -> 采正式帧
 *     4. 查找 JPEG SOI/EOI
 *     5. 保存 Flash, 发送事件
 *===========================================================================*/
void Camera_Task(void *arg)
{
    uint32_t img_id = 0;

    log_i("task started");

    /* 创建帧完成信号量和拍照请求队列 */
    xFrameDoneSem = xSemaphoreCreateBinary();
    xCaptureQueue = xQueueCreate(2, sizeof(uint8_t));

    if(xCaptureQueue == NULL || xFrameDoneSem == NULL)
    {
        log_e("semaphore create failed");
        vTaskDelete(NULL);
        return;
    }

    /*--- OV2640 初始化 ---*/
    if(OV2640_Init() != 0)
    {
        log_e("OV2640 init FAILED");
        vTaskDelete(NULL);
        return;
    }

    /*--- 配置 JPEG 模式 (必须先写 YUV422 表再写 JPEG 表, 顺序不能反) ---*/
    OV2640_JPEG_Mode();

    /* 设置 DSP 窗口和输出尺寸 */
    OV2640_ImageWin_Set(0, 0, 1600, 1200);   /* DSP 窗口 = 全尺寸 SXGA */
    OV2640_OutSize_Set(640, 480);             /* 输出 VGA 640x480 */

    /*--- 回读关键寄存器, 确认配置 ---*/
    {
        uint8_t r11, r15, r6b, r0d;
        uint8_t re0, re1, re5, rd7, rda;

        /* Sensor 寄存器组 */
        SCCB_WR_Reg(0xFF, 0x01);
        r11 = SCCB_RD_Reg(0x11);   /* CLKRC: PCLK 分频 */
        r15 = SCCB_RD_Reg(0x15);   /* COM10: HSYNC/VSYNC/PCLK 极性 */
        r6b = SCCB_RD_Reg(0x6B);   /* DBLV: PLL/PCLK 分频 */
        r0d = SCCB_RD_Reg(0x0D);   /* COM8: AGC/AWB/曝光使能 */
        log_i("OV2640 reg: 0x11=0x%02X 0x15=0x%02X 0x6B=0x%02X 0x0D=0x%02X",
              r11, r15, r6b, r0d);

        /* DSP 寄存器组: JPEG 相关 */
        SCCB_WR_Reg(0xFF, 0x00);
        re0 = SCCB_RD_Reg(0xE0);
        re1 = SCCB_RD_Reg(0xE1);
        re5 = SCCB_RD_Reg(0xE5);
        rd7 = SCCB_RD_Reg(0xD7);
        rda = SCCB_RD_Reg(0xDA);
        log_i("OV2640 JPEG DSP: E0=%02X E1=%02X E5=%02X D7=%02X DA=%02X",
              re0, re1, re5, rd7, rda);
    }

    /* 等待 OV2640 输出稳定 */
    vTaskDelay(pdMS_TO_TICKS(500));
    log_i("OV2640 JPEG 640x480 ready");

    /*--- 检测 PCLK 是否正常输出 ---*/
    {
        uint32_t transitions = 0;
        uint8_t  last, cur;
        uint32_t tick_start;

        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
        GPIOA->MODER &= ~(3u << (6*2));      /* PA6 临时设为输入 */
        last = (uint8_t)((GPIOA->IDR >> 6) & 1);
        tick_start = xTaskGetTickCount();

        /* 统计 10ms 内 PA6 电平跳变次数 */
        while(xTaskGetTickCount() - tick_start < 10)
        {
            cur = (uint8_t)((GPIOA->IDR >> 6) & 1);
            if(cur != last) { transitions++; last = cur; }
        }
        log_i("PCLK detect: PA6 transitions in 10ms = %lu", (unsigned long)transitions);
        /* My_DCMI_Init 会重新把 PA6 配成 DCMI_AF */
    }

    /*--- DCMI 初始化 ---*/
    My_DCMI_Init();
    log_i("waiting for capture command...");

    /*===================================================================
     * 主循环: 等待拍照请求 -> 采集 -> 处理 -> 保存
     *===================================================================*/
    while(1)
    {
        log_d("waiting for trigger...");

        uint8_t   mode;
        uint32_t  total_bytes = 0;
        uint8_t  *p;
        uint8_t  *jpeg_start = NULL;
        uint32_t  jpeg_size  = 0;
        uint32_t  i, j;
        uint8_t   k;
        uint8_t   attempt;
        uint8_t   got_frame = 0;

        /*--- 等待拍照请求 (阻塞) ---*/
        if(xQueueReceive(xCaptureQueue, &mode, portMAX_DELAY) != pdTRUE)
            continue;

        /*===============================================================
         * 采集循环: 最多重试 3 次
         *===============================================================*/
        for(attempt = 0; attempt < 3; attempt++)
        {
            /* 每次采集前完全复位 DCMI, 清除上一次异常状态 */
            log_d("att%d: stop", attempt + 1);
            DCMI_Stop();
            log_d("att%d: dcmi init", attempt + 1);
            My_DCMI_Init();

            /* 重置采集状态 */
            s_jpeg_data_len = 0;
            s_jpeg_data_ok  = 0;
            s_jpeg_overflow = 0;

            /* 注册 DMA TC 中断回调 */
            dcmi_rx_callback = jpeg_dcmi_rx_callback;

            log_d("att%d: dma init", attempt + 1);

            /* 第一次采集时打印缓冲区地址, 确认在普通 SRAM */
            if(attempt == 0)
            {
                log_i("camera buffers: dma0=0x%08lX dma1=0x%08lX jpeg=0x%08lX",
                      (unsigned long)s_dma_buf0,
                      (unsigned long)s_dma_buf1,
                      (unsigned long)s_jpeg_data_buf);
            }

            /* 配置 DMA 双缓冲 */
            DCMI_DMA_Init((uint32_t)s_dma_buf0,
                          (uint32_t)s_dma_buf1,
                          JPEG_DMA_BUF_WORDS,
                          DMA_MemoryDataSize_Word,
                          DMA_MemoryInc_Enable);

            log_d("att%d: start", attempt + 1);
            DCMI_Start();

            /* 调试: 启动后等 50ms, 检查 DCMI FIFO 和 DMA 是否有数据 */
            vTaskDelay(pdMS_TO_TICKS(50));
            log_i("debug: SR=0x%08lX FNE=%lu NDTR=%lu",
                  (unsigned long)DCMI->SR,
                  (unsigned long)((DCMI->SR >> 2) & 1),
                  (unsigned long)DMA2_Stream1->NDTR);

            /*--- warmup: 丢弃前 2 帧, 让 AEC/AGC/AWB 稳定 ---*/
            for(k = 0; k < 2; k++)
            {
                if(xSemaphoreTake(xFrameDoneSem, pdMS_TO_TICKS(2000)) != pdTRUE)
                {
                    log_w("warmup %d timeout (attempt %d)", k, attempt + 1);
                    break;
                }

                /* 通知中断重启 DMA, 开始下一帧 */
                s_jpeg_data_ok  = 2;
                s_jpeg_data_len = 0;
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            /*--- 采集正式帧 ---*/
            if(xSemaphoreTake(xFrameDoneSem, pdMS_TO_TICKS(2000)) != pdTRUE)
            {
                log_w("frame timeout, attempt %d", attempt + 1);
                DCMI_Stop();
                s_jpeg_data_ok = 0;
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }

            /* 帧采集完成, 停止 DCMI */
            DCMI_Stop();

            /* 检查是否溢出 */
            if(s_jpeg_overflow)
            {
                log_w("JPEG buffer overflow, attempt %d", attempt + 1);
                s_jpeg_data_ok = 0;
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }

            /* 计算帧大小 (字节) */
            total_bytes = s_jpeg_data_len * 4;

            /* 帧大小 >= 1024 字节认为有效 */
            if(total_bytes >= 1024)
            {
                got_frame = 1;
                break;   /* 采集到有效帧, 跳出重试循环 */
            }

            log_w("frame too small: %lu, attempt %d",
                  (unsigned long)total_bytes, attempt + 1);

            s_jpeg_data_ok = 0;
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        /* 3 次都失败 */
        if(!got_frame)
        {
            log_e("capture failed after 3 attempts");
            s_jpeg_data_ok = 0;
            continue;
        }

        /*===============================================================
         * 解析 JPEG: 查找 SOI(FF D8) 和 EOI(FF D9)
         *===============================================================*/
        p = (uint8_t *)s_jpeg_data_buf;

        log_i("frame %lu bytes, head: %02X %02X %02X %02X",
              (unsigned long)total_bytes, p[0], p[1], p[2], p[3]);

        jpeg_start = NULL;
        jpeg_size  = 0;

        /* 遍历查找 SOI */
        for(i = 0; i + 1 < total_bytes; i++)
        {
            if(p[i] == 0xFF && p[i+1] == 0xD8)          /* SOI: FF D8 */
            {
                jpeg_start = &p[i];
                /* 从 SOI 之后查找 EOI */
                for(j = i + 2; j + 1 < total_bytes; j++)
                {
                    if(p[j] == 0xFF && p[j+1] == 0xD9)  /* EOI: FF D9 */
                    {
                        jpeg_size = j + 2 - i;
                        break;
                    }
                }
                break;
            }
        }

        /* 未找到完整 JPEG */
        if(jpeg_start == NULL || jpeg_size < 4)
        {
            log_w("no JPEG SOI/EOI in %lu bytes", (unsigned long)total_bytes);
            s_jpeg_data_ok = 0;
            continue;
        }

        log_i("JPEG OK offset=%lu size=%lu",
              (unsigned long)(jpeg_start - p), (unsigned long)jpeg_size);

        /*===============================================================
         * 保存 Flash + 发送事件
         *===============================================================*/
        if(mode == CAM_MODE_TEST)
        {
            /* 测试模式: 仅采集, 不保存 */
        }
        else
        {
            if(save_jpeg_to_flash(jpeg_start, jpeg_size, img_id) == 0)
            {
                /* 发送图片采集完成事件 */
                Event_t evt = {0};
                evt.type         = EVT_IMAGE_CAPTURED;
                evt.data.file_id = img_id;

                if(Event_Queue != NULL && xQueueSend(Event_Queue, &evt, 0) != pdTRUE)
                    log_w("Event_Queue full");

                img_id++;
            }
        }

        s_jpeg_data_ok = 0;
    }
}
