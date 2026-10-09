#include "stm32f4xx.h"                  // Device header
#include "myled.h"


void MyLedInit(void)
{
	//第一步：开时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA,ENABLE);
	
	
	//初始化GPIOA
	GPIO_InitTypeDef S;
	S.GPIO_Mode = GPIO_Mode_OUT;
	S.GPIO_OType=GPIO_OType_PP;
	S.GPIO_Pin=GPIO_Pin_6 | GPIO_Pin_7;
	
	GPIO_Init(GPIOA,&S);
}




void MyLedOpen(void)
{
	//GPIO_Write();
	//GPIO_WriteBit();
	
	GPIO_ResetBits(GPIOA,GPIO_Pin_6 | GPIO_Pin_7);
	
}



void MyLedClose(void)
{
	GPIO_SetBits(GPIOA,GPIO_Pin_6 | GPIO_Pin_7);
	
}