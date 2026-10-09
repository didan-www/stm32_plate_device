#define LOG_TAG "dcmi"

/*===========================================================================
 * my_dcmi.c
 * ˫    ѭ   DMA + DCMI ֡ жϲɼ  JPEG
 *===========================================================================*/

#include "stm32f4xx.h"
#include "My_dcmi.h"
#include "elog.h"

volatile uint8_t g_dcmi_capturing = 0;

/* DMA TC  жϻص      ϲ ע   */
void (*dcmi_rx_callback)(void) = NULL;

/*===========================================================================
 * DCMI GPIO
 *   PA4=HSYNC  PA6=PCLK
 *   PB6=D5     PB7=VSYNC
 *   PC6=D0 PC7=D1 PC11=D4
 *   PE0=D2 PE1=D3 PE5=D6 PE6=D7
 *===========================================================================*/
static void dcmi_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_AHB1PeriphClockCmd(
        RCC_AHB1Periph_GPIOA |
        RCC_AHB1Periph_GPIOB |
        RCC_AHB1Periph_GPIOC |
        RCC_AHB1Periph_GPIOE, ENABLE);

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
 *       ģʽ  VSYNC     Ч  HSYNC     Ч  PCLK       
 *===========================================================================*/
void My_DCMI_Init(void)
{
    DCMI_InitTypeDef dcmi;
    NVIC_InitTypeDef nvic;

    dcmi_gpio_init();

    RCC_AHB2PeriphClockCmd(RCC_AHB2Periph_DCMI, ENABLE);
    DCMI_DeInit();                      /*     ϵ    λ      ʡ */

    dcmi.DCMI_CaptureMode      = DCMI_CaptureMode_Continuous;
    dcmi.DCMI_CaptureRate      = DCMI_CaptureRate_All_Frame;
    dcmi.DCMI_ExtendedDataMode = DCMI_ExtendedDataMode_8b;
    /* 与已验证代码一致：低电平有效行同步，PCLK上升沿采样。 */
    dcmi.DCMI_HSPolarity       = DCMI_HSPolarity_Low;
    dcmi.DCMI_PCKPolarity      = DCMI_PCKPolarity_Rising;
    dcmi.DCMI_SynchroMode      = DCMI_SynchroMode_Hardware;
    dcmi.DCMI_VSPolarity       = DCMI_VSPolarity_Low;
    DCMI_Init(&dcmi);

    DCMI_ITConfig(DCMI_IT_FRAME, ENABLE);
    DCMI_Cmd(ENABLE);

    nvic.NVIC_IRQChannel                   = DCMI_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 7;     /*    configMAX_SYSCALL_INTERRUPT_PRIORITY */
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    log_i("DCMI init OK (continuous, VSPol_Low, HSPol_Low, PCK_Rising)");
}

/*===========================================================================
 * DCMI_DMA_Init
 *   mem1addr != 0 ʱ    ˫    
 *===========================================================================*/
void DCMI_DMA_Init(uint32_t mem0addr, uint32_t mem1addr,
                   uint16_t memsize,
                   uint32_t memblen, uint32_t meminc)
{
    DMA_InitTypeDef dma;
	
	uint32_t guard;
	
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
    DMA_Cmd(DMA2_Stream1, DISABLE);
    DMA_DeInit(DMA2_Stream1);
	
    guard = 100000;
    while(DMA_GetCmdStatus(DMA2_Stream1) != DISABLE && guard--);
    if(guard == 0)
        log_e("DMA disable timeout in DMA_Init");
	
    dma.DMA_Channel            = DMA_Channel_1;
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DCMI->DR;
    dma.DMA_Memory0BaseAddr    = mem0addr;
    dma.DMA_DIR                = DMA_DIR_PeripheralToMemory;
    dma.DMA_BufferSize         = memsize;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = meminc;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    dma.DMA_MemoryDataSize     = memblen;
    dma.DMA_Mode               = DMA_Mode_Circular;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_FIFOMode           = DMA_FIFOMode_Enable;
    dma.DMA_FIFOThreshold      = DMA_FIFOThreshold_Full;
    dma.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
    dma.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;
    DMA_Init(DMA2_Stream1, &dma);

    if(mem1addr)
    {
        NVIC_InitTypeDef nvic;

        DMA_DoubleBufferModeCmd(DMA2_Stream1, ENABLE);
        DMA_MemoryTargetConfig(DMA2_Stream1, mem1addr, DMA_Memory_1);
        DMA_ITConfig(DMA2_Stream1, DMA_IT_TC, ENABLE);

        nvic.NVIC_IRQChannel                   = DMA2_Stream1_IRQn;
        nvic.NVIC_IRQChannelPreemptionPriority = 7;
        nvic.NVIC_IRQChannelSubPriority        = 0;
        nvic.NVIC_IRQChannelCmd                = ENABLE;
        NVIC_Init(&nvic);
    }
}

/*===========================================================================
 * DCMI_Start / DCMI_Stop
 *===========================================================================*/
void DCMI_Start(void)
{
    uint32_t guard;

    DCMI_CaptureCmd(DISABLE);
    DMA_Cmd(DMA2_Stream1, DISABLE);

    DCMI_ClearITPendingBit(DCMI_IT_FRAME | DCMI_IT_OVF | DCMI_IT_ERR);
    DMA_ClearFlag(DMA2_Stream1, DMA_FLAG_TCIF1 | DMA_FLAG_TEIF1 |
                                DMA_FLAG_HTIF1 | DMA_FLAG_FEIF1);

//    /* VSPolarity_Low  VSYNC=0        ڣ VSYNC=1         
//     *  ȵȽ      ڣ  ٵ                 ʱ        ֡  ͷ */
//    guard = 300000;
//    while(!(DCMI->SR & 0x02) && guard--);     /*    VSYNC    ߣ         */
//    guard = 300000;
//    while((DCMI->SR & 0x02) && guard--);      /*    VSYNC    ͣ   ֡  ʼ  */

    g_dcmi_capturing = 1;

    DMA_Cmd(DMA2_Stream1, ENABLE);
    DCMI_CaptureCmd(ENABLE);

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

void DCMI_Stop(void)
{
	g_dcmi_capturing = 0;   
    DCMI_CaptureCmd(DISABLE);
    /*   Ҫ while(DCMI->CR & 0x01)  ûͬ   ź ʱ    ѭ   */
    DMA_Cmd(DMA2_Stream1, DISABLE);
}

/*===========================================================================
 * DCMI ֡ ж  ->  ϲ  jpeg_data_process
 *===========================================================================*/
extern void jpeg_data_process(void);    /* Camer_Task.c ʵ   */

void DCMI_IRQHandler(void)
{
    if(DCMI_GetITStatus(DCMI_IT_FRAME) == SET)
    {
        DCMI_ClearITPendingBit(DCMI_IT_FRAME);
		
		if(g_dcmi_capturing)        //ֻ ڲɼ  ڼ䴦   
            jpeg_data_process();
    }
}

/*===========================================================================
 * DMA2 Stream1 TC  жϣ ˫     л   
 *===========================================================================*/
void DMA2_Stream1_IRQHandler(void)
{
    if(DMA_GetFlagStatus(DMA2_Stream1, DMA_FLAG_TCIF1) == SET)
    {
        DMA_ClearFlag(DMA2_Stream1, DMA_FLAG_TCIF1);
        if(dcmi_rx_callback != NULL)
            dcmi_rx_callback();
    }
}
