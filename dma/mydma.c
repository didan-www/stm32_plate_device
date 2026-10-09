#include "stm32f4xx.h"                  // Device header
#include "string.h"
#include "mydma.h"
/*    ڴ  -->   贮   */


const uint8_t DMA_SendBuf[] = "As flowers fall and fly across the skies,\
    Who rues the red that fades,the scent that dies?Softly the gossamer floats over bowers green;\
    Gently the willow fluff wafts to broidered screen.\
    In my chamber i am grieved to see spring depart.\
    Where can I pour out my sorrow-laden heart?\n";


void Usart1_DMA_Init(void)
{
	//1.  ʱ ӣ GPIOA + USART1  
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1,ENABLE);
	
	
	//GPIO     
	//2.        PA9            
	GPIO_InitTypeDef S1;
	S1.GPIO_Mode = GPIO_Mode_AF;  //    
	S1.GPIO_OType = GPIO_OType_PP;  //    
    S1.GPIO_Pin = GPIO_Pin_9;
	S1.GPIO_Speed = GPIO_Speed_50MHz; 
	GPIO_Init(GPIOA,&S1);
	
	//3.PA10         
	S1.GPIO_Mode = GPIO_Mode_AF;
	S1.GPIO_Pin = GPIO_Pin_10;
	S1.GPIO_PuPd = GPIO_PuPd_UP;  //        
	GPIO_Init(GPIOA,&S1);
	
	//4.ָ          
	GPIO_PinAFConfig(GPIOA,GPIO_PinSource9,GPIO_AF_USART1);
	GPIO_PinAFConfig(GPIOA,GPIO_PinSource10,GPIO_AF_USART1);
	
	//5.    usart1 Ĳ   
	
	USART_InitTypeDef S2;
	S2.USART_BaudRate = 115200;  //      
	S2.USART_HardwareFlowControl = USART_HardwareFlowControl_None;  //Ӳ      (    Ҫ  
	S2.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; // շ ģʽ
	S2.USART_Parity = USART_Parity_No; //У 飨 ޣ 
	S2.USART_StopBits = USART_StopBits_1;  //ֹͣλ
	S2.USART_WordLength = USART_WordLength_8b;  //  Ч   ݳ   
	USART_Init(USART1,&S2);
	
	//ʹ ܴ   1  DMA    
	USART_DMACmd(USART1,USART_DMAReq_Tx,ENABLE);
	
	//6.ʹ ܴ   
	USART_Cmd(USART1,ENABLE);
}

void Usart1_DMA_Send(const uint8_t *buf, uint16_t len)
{
    if(buf == NULL || len == 0) return;
    DMA_Cmd(DMA2_Stream7, DISABLE);
    while(DMA_GetCmdStatus(DMA2_Stream7) != DISABLE);
    DMA2_Stream7->M0AR = (uint32_t)buf;
    DMA2_Stream7->NDTR = len;
    DMA_ClearFlag(DMA2_Stream7, DMA_FLAG_TCIF7 | DMA_FLAG_TEIF7 |
                  DMA_FLAG_HTIF7 | DMA_FLAG_FEIF7 | DMA_FLAG_DMEIF7);
    DMA_Cmd(DMA2_Stream7, ENABLE);
}

void DMA2_Stream1_Chan4_Init(void)
{
    DMA2_Stream7_Chan4_Init();
}

void DMA2_Stream7_Chan4_Init(void)
{
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2,ENABLE);
	
	// ر   
	DMA_Cmd(DMA2_Stream7,DISABLE);
	while(DMA_GetCmdStatus(DMA2_Stream7) != DISABLE);
	
	//dma    
	
	DMA_InitTypeDef S;
	S.DMA_BufferSize = strlen((char *)DMA_SendBuf); //          ݳ   
	S.DMA_Channel = DMA_Channel_4;
	S.DMA_DIR = DMA_DIR_MemoryToPeripheral; //   ䷽    ڴ  ->     
	S.DMA_FIFOMode = DMA_FIFOMode_Enable; // Ƿ     FIFO
	S.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull; //FIFO    ֵ
	S.DMA_Memory0BaseAddr = (uint32_t)DMA_SendBuf; // ڴ    ʼ  ַ
	S.DMA_MemoryBurst =	DMA_MemoryBurst_Single; // ڴ  ͻ      
	S.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte; // 洢     ݿ   
	S.DMA_MemoryInc = DMA_MemoryInc_Enable; // ڴ     
	S.DMA_Mode = DMA_Mode_Normal;  //   δ   
	S.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;  //   ڵ    ݼĴ   
	S.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;//   費ͻ  
	S.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; //       ݿ   
	S.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	S.DMA_Priority = DMA_Priority_VeryHigh; //   ȼ 
	DMA_Init(DMA2_Stream7,&S);

}
