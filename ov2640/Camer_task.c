#define LOG_TAG "camera"

/*===========================================================================
 * Camer_Task.c
 *
 *         
 *   OV2640 JPEG    DCMI    DMA2 Stream1 ˫   壨buf0/buf1    4KB  
 *           DMA TC  жϣ ÿ 4KB һ Σ 
 *   jpeg_dcmi_rx_callback()    ׷ ӵ  s_jpeg_data_buf[]
 *           DCMI FRAME  жϣ ÿ֡һ Σ 
 *   jpeg_data_process()          β     s_jpeg_data_ok=1
 *           Camera_Task    ź       
 *        FF D8 ... FF D9       Flash    Ͷ ¼ 
 *===========================================================================*/

#include "stm32f4xx.h"
#include "Camer_task.h"
#include "Ov2640.h"
#include "My_dcmi.h"
#include "w25q64.h"
#include "ff.h"
#include "app.h"
#include "mqtt_app.h"
#include "elog.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include <string.h>
#include <stdio.h>

/*===========================================================================
 *         uint32_t    飬ʵ   ֽ    = Ԫ        4  
 *
 *   DMA ˫   壺 1024       4 = 4KB     鹲 8KB
 *    ۻ    壺  10240       4 = 40KB
 *
 *        SRAM1/SRAM2         CCM(0x10000000)      DMA    ʲ    CCM  
 *         .map      s_jpeg_data_buf    ַӦΪ 0x2000xxxx  
 *===========================================================================*/
#define JPEG_DMA_BUF_WORDS    (1  * 1024)
#define JPEG_DATA_BUF_WORDS   (10 * 1024)

/* DMA2只能访问普通SRAM，不能访问CCM。通过独立段配合scatter文件固定到SRAM。 */
static uint32_t __attribute__((section("DMA_SRAM"), aligned(4)))
    s_dma_buf0[JPEG_DMA_BUF_WORDS];
static uint32_t __attribute__((section("DMA_SRAM"), aligned(4)))
    s_dma_buf1[JPEG_DMA_BUF_WORDS];
static uint32_t __attribute__((section("DMA_SRAM"), aligned(4)))
    s_jpeg_data_buf[JPEG_DATA_BUF_WORDS];

/*
 * s_jpeg_data_ok:  0=δ     1=  ɴ       2= Ѵ      ز 
 * s_jpeg_data_len:    ۻ       
 */
static volatile uint32_t s_jpeg_data_len = 0;
static volatile uint8_t  s_jpeg_data_ok  = 0;
static volatile uint8_t  s_jpeg_overflow = 0;

TaskHandle_t Camera_Task_Handle = NULL;
static SemaphoreHandle_t xFrameDoneSem = NULL;

static QueueHandle_t xCaptureQueue = NULL;
static volatile uint8_t s_capture_busy = 0;

static volatile uint8_t  s_capture_mode = 0;

extern QueueHandle_t Event_Queue;

/*===========================================================================
 * jpeg_data_process
 *    DCMI ֡ жϵ   
 *===========================================================================*/
void jpeg_data_process(void)
{
    uint16_t  rlen, i;
    uint32_t *pbuf;

    if(s_jpeg_data_ok == 0)         /*   ǰ֡  δ ɼ    */
    {
        DMA_Cmd(DMA2_Stream1, DISABLE);
        uint32_t guard = 10000;
		while(DMA_GetCmdStatus(DMA2_Stream1) != DISABLE  && guard--)
		{		
			
		}

        /*   ǰ    д        */
        rlen = (uint16_t)(JPEG_DMA_BUF_WORDS - DMA_GetCurrDataCounter(DMA2_Stream1));
        if(rlen > JPEG_DMA_BUF_WORDS) rlen = JPEG_DMA_BUF_WORDS;
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

        pbuf = s_jpeg_data_buf + s_jpeg_data_len;

        /* CT=1   ʾ DMA     д M1    ô  ǰδ       M1 */
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

        {
            BaseType_t woken = pdFALSE;
            if(xFrameDoneSem != NULL)
                xSemaphoreGiveFromISR(xFrameDoneSem, &woken);
            portYIELD_FROM_ISR(woken);
        }
    }

    if(s_jpeg_data_ok == 2)         /*   һ֡ Ѵ        ¿ ʼ */
    {
        DMA_SetCurrDataCounter(DMA2_Stream1, JPEG_DMA_BUF_WORDS);
        DMA_Cmd(DMA2_Stream1, ENABLE);
        s_jpeg_data_ok  = 0;
        s_jpeg_data_len = 0;
    }
}

/*===========================================================================
 * jpeg_dcmi_rx_callback
 *    DMA TC  жϵ  ã ÿ    һ   4KB ׷ ӵ  ۻ     
 *===========================================================================*/
static void jpeg_dcmi_rx_callback(void)
{
    uint32_t *pbuf;
    uint16_t  i;

    if(s_jpeg_data_ok != 0) return;         /*   ǰ֡      */

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

    /* CT=1   ʾ DMA    е  M1    ô           M0 */
    if(DMA2_Stream1->CR & (1 << 19))
    {
        for(i = 0; i < JPEG_DMA_BUF_WORDS; i++) pbuf[i] = s_dma_buf0[i];
    }
    else
    {
        for(i = 0; i < JPEG_DMA_BUF_WORDS; i++) pbuf[i] = s_dma_buf1[i];
    }

    s_jpeg_data_len += JPEG_DMA_BUF_WORDS;

    /* 正常帧只能由DCMI FRAME中断结束，不能按固定长度提前截断。 */
}

/*===========================================================================
 *     ӿ 
 *===========================================================================*/
void Camera_TriggerCapture(uint8_t mode)
{
    if(xCaptureQueue == NULL) return;

    if(!MQTT_IsConnected())
    {
        log_w("capture ignored: MQTT not ready");
        return;
    }

    /* 一次只允许一张图片进入采集/保存/上传链路，避免请求堆积。 */
    if(s_capture_busy)
    {
        log_w("capture ignored: capture already busy");
        return;
    }

    if(xQueueSend(xCaptureQueue, &mode, 0) != pdTRUE)
    {
        log_w("capture queue FULL");
        log_w("  waiting=%u, task state=%d",
              (unsigned)uxQueueMessagesWaiting(xCaptureQueue),
              (Camera_Task_Handle != NULL) ?
                  eTaskGetState(Camera_Task_Handle) : -1);
    }
    else
    {
        s_capture_busy = 1;
        log_d("capture queued");
    }
}

uint8_t  *Camera_GetFrameBuf(void)  { return (uint8_t *)s_jpeg_data_buf; }
uint32_t  Camera_GetFrameSize(void) { return s_jpeg_data_len * 4; }

/*===========================================================================
 *  浽 W25Q64  FatFs  
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
 *
 * ֱ   滻 Camer_Task.c         Camera_Task     
 *
 *  ؼ   ƣ 
 *   1. ÿ βɼ ǰ   My_DCMI_Init()   ȫ  λ DCMI      ϴβ   ״̬
 *   2.         3  Σ ÿ     ³ ʼ        һ        ֡  ͷ
 *   3. ÿ   õ ֡        s_jpeg_data_ok = 2       жϲ        DMA
 *   4.      < 1024 ֱ    ʧ     ԣ    ˷Ѻ   Ĳ       
 *=========================================================================== */
void Camera_Task(void *arg)
{
    uint32_t img_id = 0;

    log_i("task started");
    xFrameDoneSem = xSemaphoreCreateBinary();
    xCaptureQueue = xQueueCreate(1, sizeof(uint8_t));
	
    if(xCaptureQueue == NULL || xFrameDoneSem == NULL)
    {
        log_e("semaphore create failed");
        vTaskDelete(NULL);
        return;
    }

    /*--- 配置 MCO1(PA8) 输出 HSE 时钟给 OV2640 XCLK ---*/
    {
        GPIO_InitTypeDef gpio;
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
        gpio.GPIO_Pin   = GPIO_Pin_8;
        gpio.GPIO_Mode  = GPIO_Mode_AF;
        gpio.GPIO_Speed = GPIO_Speed_100MHz;
        gpio.GPIO_OType = GPIO_OType_PP;
        gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL;
        GPIO_Init(GPIOA, &gpio);
        GPIO_PinAFConfig(GPIOA, GPIO_PinSource8, GPIO_AF_MCO);
        RCC_MCO1Config(RCC_MCO1Source_HSE, RCC_MCO1Div_1);  /* PA8 输出 HSE */
        log_i("MCO1 enabled on PA8 (HSE)");
    }

    /*--- OV2640 初始化 ---*/
    if(OV2640_Init() != 0)
    {
        log_e("OV2640 init FAILED");
        vTaskDelete(NULL);
        return;
    }

    /*---   ʽ   ã        YUV422    JPEG  ˳   ܷ  ---*/
    OV2640_JPEG_Mode();

    OV2640_ImageWin_Set(0, 0, 1600, 1200);      /* DSP 窗口 = 全尺寸 */
    OV2640_OutSize_Set(640, 480);               /* 输出 VGA */

    /* 回读关键寄存器，分别确认Sensor时序和DSP JPEG模式。 */
    {
        uint8_t r11, r15, r6b, r0d;
        uint8_t re0, re1, re5, rd7, rda;
        SCCB_WR_Reg(0xFF, 0x01);  /* 切 Sensor 寄存器组 */
        r11 = SCCB_RD_Reg(0x11);  /* CLKRC */
        r15 = SCCB_RD_Reg(0x15);  /* COM10 (HSYNC/VSYNC/PCLK 极性) */
        r6b = SCCB_RD_Reg(0x6B);  /* DBLV (PLL/PCLK 分频) */
        r0d = SCCB_RD_Reg(0x0D);  /* COM8 */
        log_i("OV2640 reg: 0x11=0x%02X 0x15=0x%02X 0x6B=0x%02X 0x0D=0x%02X",
              r11, r15, r6b, r0d);

        SCCB_WR_Reg(0xFF, 0x00);  /* 切DSP寄存器组 */
        re0 = SCCB_RD_Reg(0xE0);
        re1 = SCCB_RD_Reg(0xE1);
        re5 = SCCB_RD_Reg(0xE5);
        rd7 = SCCB_RD_Reg(0xD7);
        rda = SCCB_RD_Reg(0xDA);
        log_i("OV2640 JPEG DSP: E0=%02X E1=%02X E5=%02X D7=%02X DA=%02X",
              re0, re1, re5, rd7, rda);
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    log_i("OV2640 JPEG 640x480 ready");

    /* 检测 PA6 上是否有 PCLK 信号：临时配成输入，统计10ms内电平跳变次数 */
    {
        uint32_t transitions = 0;
        uint8_t  last, cur;
        uint32_t tick_start;

        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
        GPIOA->MODER &= ~(3u << (6*2));   /* PA6 设为输入 */
        last = (uint8_t)((GPIOA->IDR >> 6) & 1);
        tick_start = xTaskGetTickCount();
        while(xTaskGetTickCount() - tick_start < 10)  /* 10ms */
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

        /*---  ȴ    ---*/
		if(xQueueReceive(xCaptureQueue, &mode, portMAX_DELAY) != pdTRUE)
			continue;
        ///mode = s_capture_mode;
		

        /*=====================================================
         *  ɼ           3   
         *=====================================================*/
        for(attempt = 0; attempt < 3; attempt++)
        {
            /*   ȫ  λ DCMI      ϴ       ״̬ */
			log_d("att%d: stop", attempt + 1);
            DCMI_Stop();
			log_d("att%d: dcmi init", attempt + 1);
            My_DCMI_Init();

            s_jpeg_data_len = 0;
            s_jpeg_data_ok  = 0;
            s_jpeg_overflow = 0;

            dcmi_rx_callback = jpeg_dcmi_rx_callback;
			
            log_d("att%d: dma init", attempt + 1); 
			if(attempt == 0)
			{
				log_i("camera buffers: dma0=0x%08lX dma1=0x%08lX jpeg=0x%08lX",
				      (unsigned long)s_dma_buf0,
				      (unsigned long)s_dma_buf1,
				      (unsigned long)s_jpeg_data_buf);
			}
			
            DCMI_DMA_Init((uint32_t)s_dma_buf0,
                          (uint32_t)s_dma_buf1,
                          JPEG_DMA_BUF_WORDS,
                          DMA_MemoryDataSize_Word,
                          DMA_MemoryInc_Enable);
			log_d("att%d: start", attempt + 1);
            DCMI_Start();

            /* 调试：启动后等50ms，检查DCMI FIFO和DMA是否有数据 */
            vTaskDelay(pdMS_TO_TICKS(50));
            log_i("debug: SR=0x%08lX FNE=%lu NDTR=%lu",
                  (unsigned long)DCMI->SR,
                  (unsigned long)((DCMI->SR >> 2) & 1),
                  (unsigned long)DMA2_Stream1->NDTR);

            /*--- 先丢弃一帧，等待同步稳定后再采集下一帧 ---*/
            for(k = 0; k < 1; k++)
            {
                if(xSemaphoreTake(xFrameDoneSem, pdMS_TO_TICKS(3000)) != pdTRUE)
                {
                    log_w("warmup %d timeout (attempt %d)", k, attempt + 1);
                    break;
                }

                /* ֪ͨ ж      DMA    һ      ʡ */
                s_jpeg_data_ok  = 2;
                s_jpeg_data_len = 0;
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            /*---   ʽ֡ ---*/
            if(xSemaphoreTake(xFrameDoneSem, pdMS_TO_TICKS(3000)) != pdTRUE)
            {
                log_w("frame timeout, attempt %d", attempt + 1);
                DCMI_Stop();
                s_jpeg_data_ok = 0;
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }

            DCMI_Stop();

            if(s_jpeg_overflow)
            {
                log_w("JPEG buffer overflow, attempt %d", attempt + 1);
                s_jpeg_data_ok = 0;
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }

            total_bytes = s_jpeg_data_len * 4;

            if(total_bytes >= 1024)
            {
                got_frame = 1;
                break;                          /*  õ   Ч֡ */
            }

            log_w("frame too small: %lu, attempt %d",
                  (unsigned long)total_bytes, attempt + 1);

            s_jpeg_data_ok = 0;
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if(!got_frame)
        {
            log_e("capture failed after 3 attempts");
            s_jpeg_data_ok = 0;
            s_capture_busy = 0;
            continue;
        }

        /*=====================================================
         *      JPEG
         *=====================================================*/
        p = (uint8_t *)s_jpeg_data_buf;

        log_i("frame %lu bytes, head: %02X %02X %02X %02X",
              (unsigned long)total_bytes, p[0], p[1], p[2], p[3]);

        jpeg_start = NULL;
        jpeg_size  = 0;

        for(i = 0; i + 1 < total_bytes; i++)
        {
            if(p[i] == 0xFF && p[i+1] == 0xD8)          /* SOI */
            {
                jpeg_start = &p[i];
                for(j = i + 2; j + 1 < total_bytes; j++)
                {
                    if(p[j] == 0xFF && p[j+1] == 0xD9)  /* EOI */
                    {
                        jpeg_size = j + 2 - i;
                        break;
                    }
                }
                break;
            }
        }

        if(jpeg_start == NULL || jpeg_size < 4)
        {
            log_w("no JPEG SOI/EOI in %lu bytes", (unsigned long)total_bytes);
            s_jpeg_data_ok = 0;
            s_capture_busy = 0;
            continue;
        }

        log_i("JPEG OK offset=%lu size=%lu",
              (unsigned long)(jpeg_start - p), (unsigned long)jpeg_size);

        /*=====================================================
         *   ģʽ    
         *=====================================================*/
        if(mode == CAM_MODE_TEST)
        {
            /* ֻ  ֤         */
        }
        else
        {
            if(save_jpeg_to_flash(jpeg_start, jpeg_size, img_id) == 0)
            {
                Event_t evt = {0};              /*               */
                evt.type         = EVT_IMAGE_CAPTURED;
                evt.data.file_id = img_id;

                if(Event_Queue != NULL && xQueueSend(Event_Queue, &evt, 0) != pdTRUE)
                    log_w("Event_Queue full");

                img_id++;
            }
        }

        s_jpeg_data_ok = 0;
        s_capture_busy = 0;
    }
}

