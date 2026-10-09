#include "stm32f4xx.h"                  // Device header
#include "stdio.h"
#include "myled.h"


void MyUsart1Init(void)
{
	//开时钟（GPIOA+USART1）
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1,ENABLE);
	
	//GPIO配置
	//单独配置PA9  复用推挽输出
	GPIO_InitTypeDef S1;
	S1.GPIO_Mode = GPIO_Mode_AF;
	S1.GPIO_OType=GPIO_OType_PP;
	S1.GPIO_Pin=GPIO_Pin_9;
	S1.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&S1);
	
	
	//PA10  复用输入
	S1.GPIO_Mode = GPIO_Mode_AF;
	S1.GPIO_Pin=GPIO_Pin_10;
	S1.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOA,&S1);
	
	
	//指定复用外设
	GPIO_PinAFConfig(GPIOA,GPIO_PinSource9,GPIO_AF_USART1);
	GPIO_PinAFConfig(GPIOA,GPIO_PinSource10,GPIO_AF_USART1);
	
	
	//配置usart1的参数
	USART_InitTypeDef S2;
	S2.USART_BaudRate = 115200;//波特率
	S2.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//硬件流控(不需要)
	S2.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;//收发模式
	S2.USART_Parity = USART_Parity_No;//校验位
	S2.USART_StopBits = USART_StopBits_1;//停止位
	S2.USART_WordLength = USART_WordLength_8b;//有效数据长度
	
	USART_Init(USART1,&S2);
	
	
	//串口中断配置
	NVIC_InitTypeDef S3;
	S3.NVIC_IRQChannel=USART1_IRQn;
	S3.NVIC_IRQChannelCmd=ENABLE;
	S3.NVIC_IRQChannelPreemptionPriority=0;//抢占
	S3.NVIC_IRQChannelSubPriority=0;
	NVIC_Init(&S3);
	
	//使能串口的接收中断
	USART_ITConfig(USART1,USART_IT_RXNE,ENABLE);
	USART_ITConfig(USART1,USART_IT_IDLE,ENABLE);//IDLE中断使能
	
	//使能串口
	USART_Cmd(USART1,ENABLE);
	
}


//printf重定向
int fputc(int ch,FILE*f)
{
	//(usart1_senddata,(uint8_t)ch);
	
	while(USART_GetFlagStatus(USART1,USART_FLAG_TXE)==RESET);
	USART_SendData(USART1,(uint8_t)ch);
	return ch;
}



void Usart1_SendString(uint8_t *string)
{
	while(*string!='\0')
	{
		while(USART_GetFlagStatus(USART1,USART_FLAG_TXE)==RESET);
		USART_SendData(USART1,(uint8_t)*string++);
		
	}
	
	
}

// 轮询法接收一个字节（超时返回 0）
uint8_t USART1_ReceiveByte(uint8_t *data,uint32_t timeout) {
    uint32_t tick = 0;
    while (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == RESET) {
        tick++;
        if (tick > timeout) return 0; // 超时返回,未收到数据
    }
    *data = (uint8_t)USART_ReceiveData(USART1);
    return 1;
}



		
	

