/**
 ******************************************************************************
 * @file    my_dcmi.c
 * @brief   DCMI + DMA2_Stream1 双缓冲循环采集驱动
 *
 *  采集流程:
 *    1. My_DCMI_Init()  配置 DCMI 外设和 GPIO
 *    2. DCMI_DMA_Init() 配置 DMA 双缓冲
 *    3. DCMI_Start()    启动采集, DMA 自动搬运 DCMI 数据到内存
 *    4. DMA TC 中断     每填满一个缓冲区触发, 调用 dcmi_rx_callback()
 *    5. DCMI FRAME 中断 一帧结束触发, 调用 jpeg_data_process()
 *    6. DCMI_Stop()     停止采集
 *
 *  中断优先级: 7 (低于 configMAX_SYSCALL_INTERRUPT_PRIORITY, 可安全调用 FreeRTOS API)
 ******************************************************************************
 */
#define LOG_TAG "dcmi"

#include "stm32f4xx.h"
#include "My_dcmi.h"
#include "elog.h"

/* 采集状态标志 */
volatile uint8_t g_dcmi_capturing = 0;

/* DMA TC 中断回调函数指针, 由上层注册 (如 jpeg_dcmi_rx_callback) */
void (*dcmi_rx_callback)(void) = NULL;

/*===========================================================================
 * DCMI GPIO 初始化
 *   PA4=HSYNC  PA6=PCLK
 *   PB6=D5     PB7=VSYNC
 *   PC6=D0 PC7=D1 PC11=D4
 *   PE0=D2 PE1=D3 PE5=D6 PE6=D7
 *===========================================================================*/
static void dcmi_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    /* 使能 GPIO 时钟 */
    RCC_AHB1PeriphClockCmd(
        RCC_AHB1Periph_GPIOA |
        RCC_AHB1Periph_GPIOB |
        RCC_AHB1Periph_GPIOC |
        RCC_AHB1Periph_GPIOE, ENABLE);

    /* 统一配置: 复用功能, 推挽, 100MHz, 上拉 */
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_Speed = GPIO_Speed_100MHz;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;

    /* PA4=HSYNC  PA6=PCLK */
    gpio.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_6;
    GPIO_Init(GPIOA, &gpio);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource4, GPIO_AF_DCMI);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_DCMI);

    /* PB6=D5  PB7=VSYNC */
    gpio.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_Init(GPIOB, &gpio);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource6, GPIO_AF_DCMI);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource7, GPIO_AF_DCMI);

    /* PC6=D0  PC7=D1  PC11=D4 */
    gpio.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_11;
    GPIO_Init(GPIOC, &gpio);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource6,  GPIO_AF_DCMI);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource7,  GPIO_AF_DCMI);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource11, GPIO_AF_DCMI);

    /* PE0=D2  PE1=D3  PE5=D6  PE6=D7 */
    gpio.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_5 | GPIO_Pin_6;
    GPIO_Init(GPIOE, &gpio);
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource0, GPIO_AF_DCMI);
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource1, GPIO_AF_DCMI);
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource5, GPIO_AF_DCMI);
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource6, GPIO_AF_DCMI);
}

/*===========================================================================
 * My_DCMI_Init
 *   连续采集模式, VSYNC低有效, HSYNC低有效, PCLK上升沿采样
 *===========================================================================*/
void My_DCMI_Init(void)
{
    DCMI_InitTypeDef dcmi;
    NVIC_InitTypeDef nvic;

    /* 配置 GPIO */
    dcmi_gpio_init();

    /* 使能 DCMI 时钟 */
    RCC_AHB2PeriphClockCmd(RCC_AHB2Periph_DCMI, ENABLE);

    /* 复位 DCMI, 确保从上一次异常状态恢复 */
    DCMI_DeInit();

    /* DCMI 参数配置 */
    dcmi.DCMI_CaptureMode      = DCMI_CaptureMode_Continuous;   /* 连续采集 */
    dcmi.DCMI_CaptureRate      = DCMI_CaptureRate_All_Frame;    /* 捕获所有帧 */
    dcmi.DCMI_ExtendedDataMode = DCMI_ExtendedDataMode_8b;      /* 8位数据接口 */
    dcmi.DCMI_HSPolarity       = DCMI_HSPolarity_Low;           /* HSYNC低有效 */
    dcmi.DCMI_PCKPolarity      = DCMI_PCKPolarity_Rising;       /* PCLK上升沿采样 */
    dcmi.DCMI_SynchroMode      = DCMI_SynchroMode_Hardware;     /* 硬件同步 */
    dcmi.DCMI_VSPolarity       = DCMI_VSPolarity_Low;           /* VSYNC低有效 */
    DCMI_Init(&dcmi);

    /* 使能帧中断 */
    DCMI_ITConfig(DCMI_IT_FRAME, ENABLE);

    /* 使能 DCMI 外设 (注意: 此时 CAPTURE 位未置1, 不开始采集) */
    DCMI_Cmd(ENABLE);

    /* 配置 DCMI 中断 NVIC */
    nvic.NVIC_IRQChannel                   = DCMI_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 7;  /* 低于 configMAX_SYSCALL_INTERRUPT_PRIORITY */
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    log_i("DCMI init OK (continuous, VSPol_Low, HSPol_Low, PCK_Rising)");
}

/*===========================================================================
 * DCMI_DMA_Init
 *   mem1addr != 0 时启用双缓冲模式
 *===========================================================================*/
void DCMI_DMA_Init(uint32_t mem0addr, uint32_t mem1addr,
                   uint16_t memsize,
                   uint32_t memblen, uint32_t meminc)
{
    DMA_InitTypeDef dma;
    uint32_t guard;

    /* 使能 DMA2 时钟 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);

    /* 先关闭 DMA 流 */
    DMA_Cmd(DMA2_Stream1, DISABLE);
    DMA_DeInit(DMA2_Stream1);

    /* 等待 DMA 流完全禁用 (防止上一次传输未结束) */
    guard = 100000;
    while(DMA_GetCmdStatus(DMA2_Stream1) != DISABLE && guard--);
    if(guard == 0)
        log_e("DMA disable timeout in DMA_Init");

    /* DMA 流参数配置 */
    dma.DMA_Channel            = DMA_Channel_1;                    /* DCMI 固定用 Channel1 */
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DCMI->DR;              /* 外设源: DCMI 数据寄存器 */
    dma.DMA_Memory0BaseAddr    = mem0addr;                         /* 内存目标0 */
    dma.DMA_DIR                = DMA_DIR_PeripheralToMemory;       /* 方向: 外设->内存 */
    dma.DMA_BufferSize         = memsize;                          /* 缓冲区大小 (word数) */
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;        /* 外设地址不增量 */
    dma.DMA_MemoryInc          = meminc;                           /* 内存地址增量 */
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;      /* 外设数据宽度: 32位 */
    dma.DMA_MemoryDataSize     = memblen;                          /* 内存数据宽度 */
    dma.DMA_Mode               = DMA_Mode_Circular;                /* 循环模式 */
    dma.DMA_Priority           = DMA_Priority_High;                /* 高优先级 */
    dma.DMA_FIFOMode           = DMA_FIFOMode_Enable;              /* 使能 FIFO */
    dma.DMA_FIFOThreshold      = DMA_FIFOThreshold_Full;           /* FIFO 满时触发传输 */
    dma.DMA_MemoryBurst        = DMA_MemoryBurst_Single;            /* 内存单次突发 */
    dma.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;        /* 外设单次突发 */
    DMA_Init(DMA2_Stream1, &dma);

    /* 如果提供了第二个缓冲区, 启用双缓冲模式 */
    if(mem1addr)
    {
        NVIC_InitTypeDef nvic;

        DMA_DoubleBufferModeCmd(DMA2_Stream1, ENABLE);
        DMA_MemoryTargetConfig(DMA2_Stream1, mem1addr, DMA_Memory_1);

        /* 使能传输完成中断 */
        DMA_ITConfig(DMA2_Stream1, DMA_IT_TC, ENABLE);

        /* 配置 DMA 中断 NVIC */
        nvic.NVIC_IRQChannel                   = DMA2_Stream1_IRQn;
        nvic.NVIC_IRQChannelPreemptionPriority = 7;
        nvic.NVIC_IRQChannelSubPriority        = 0;
        nvic.NVIC_IRQChannelCmd                = ENABLE;
        NVIC_Init(&nvic);
    }
}

/*===========================================================================
 * DCMI_Start
 *   启动顺序: 关采集 -> 关DMA -> 清标志 -> 开DMA -> 开采集
 *===========================================================================*/
void DCMI_Start(void)
{
    /* 先停止上一次采集 */
    DCMI_CaptureCmd(DISABLE);
    DMA_Cmd(DMA2_Stream1, DISABLE);

    /* 清除 DCMI 中断标志 */
    DCMI_ClearITPendingBit(DCMI_IT_FRAME | DCMI_IT_OVF | DCMI_IT_ERR);

    /* 清除 DMA 中断标志 */
    DMA_ClearFlag(DMA2_Stream1, DMA_FLAG_TCIF1 | DMA_FLAG_TEIF1 |
                                DMA_FLAG_HTIF1 | DMA_FLAG_FEIF1);

    /* 标记正在采集 */
    g_dcmi_capturing = 1;

    /* 先开 DMA, 再开 DCMI 采集 (确保 DMA 准备好接收数据) */
    DMA_Cmd(DMA2_Stream1, ENABLE);
    DCMI_CaptureCmd(ENABLE);

    /* 调试: 打印启动时寄存器状态 */
    log_i("DCMI_Start: CR=0x%08lX SR=0x%08lX DMA_CR=0x%08lX NDTR=%lu",
          (unsigned long)DCMI->CR,
          (unsigned long)DCMI->SR,
          (unsigned long)DMA2_Stream1->CR,
          (unsigned long)DMA2_Stream1->NDTR);
    log_i("GPIO: PA6 MODER=0x%08lX AFR_L=0x%08lX (PA6_AF=%lu)",
          (unsigned long)GPIOA->MODER,
          (unsigned long)GPIOA->AFR[0],
          (unsigned long)((GPIOA->AFR[0] >> 24) & 0x0F));
}

/*===========================================================================
 * DCMI_Stop
 *   停止顺序: 关采集 -> 关DMA
 *===========================================================================*/
void DCMI_Stop(void)
{
    g_dcmi_capturing = 0;
    DCMI_CaptureCmd(DISABLE);
    DMA_Cmd(DMA2_Stream1, DISABLE);
}

/*===========================================================================
 * DCMI 帧中断服务函数
 *   一帧采集完成后触发, 调用 jpeg_data_process() 处理帧尾数据
 *===========================================================================*/
extern void jpeg_data_process(void);    /* 在 Camer_task.c 中实现 */

void DCMI_IRQHandler(void)
{
    if(DCMI_GetITStatus(DCMI_IT_FRAME) == SET)
    {
        DCMI_ClearITPendingBit(DCMI_IT_FRAME);

        /* 只在采集状态下处理, 避免停止后残留中断 */
        if(g_dcmi_capturing)
            jpeg_data_process();
    }
}

/*===========================================================================
 * DMA2 Stream1 传输完成中断服务函数
 *   双缓冲模式下, 每填满一个缓冲区触发一次, 调用注册的回调函数
 *===========================================================================*/
void DMA2_Stream1_IRQHandler(void)
{
    if(DMA_GetFlagStatus(DMA2_Stream1, DMA_FLAG_TCIF1) == SET)
    {
        DMA_ClearFlag(DMA2_Stream1, DMA_FLAG_TCIF1);

        /* 调用上层注册的回调 (如 jpeg_dcmi_rx_callback) */
        if(dcmi_rx_callback != NULL)
            dcmi_rx_callback();
    }
}
