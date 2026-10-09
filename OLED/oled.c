#include "stm32f4xx.h"                  // Device header
#include "MyIIC.h"
#include "stdio.h"

extern uint8_t oled_arr[8][128];

//命令发送函数
//从机地址0x78-----先发送0x00
uint8_t IIC_SendCommondToOled(uint8_t Commond)
{
	IIC_Start();
	
	IIC_SendByte(0x78);
	if(IICWaitAck())goto err;
	
	IIC_SendByte(0x00);
	if(IICWaitAck())goto err;
	
	IIC_SendByte(Commond);
	if(IICWaitAck())goto err;
	
	IIC_Stop();
	return 0;
	err:
	IIC_Stop();
	printf("[IIC]send to oled err\n");
	return 1;
}


uint8_t IIC_SendDataToOled(uint8_t data)
{
	IIC_Start();
	
	IIC_SendByte(0x78);
	if(IICWaitAck())goto err;
	
	IIC_SendByte(0x40);
	if(IICWaitAck())goto err;
	
	IIC_SendByte(data);
	if(IICWaitAck())goto err;
	
	IIC_Stop();
	return 0;
	err:
	IIC_Stop();
	printf("[IIC]send data to oled err\n");
	return 1;
	
}

void OLED_Init(void)
{
	IIC_SendCommondToOled(0xAE); // 关闭显示
	IIC_SendCommondToOled(0xD5);
	IIC_SendCommondToOled(0x80);
	IIC_SendCommondToOled(0xA8);
	IIC_SendCommondToOled(0x3F);
	IIC_SendCommondToOled(0xD3);
	IIC_SendCommondToOled(0x00);
	IIC_SendCommondToOled(0x40);
	IIC_SendCommondToOled(0xA1);
	IIC_SendCommondToOled(0xC8);
	IIC_SendCommondToOled(0xDA);
	IIC_SendCommondToOled(0x12);
	IIC_SendCommondToOled(0x81);
	IIC_SendCommondToOled(0xCF);
	IIC_SendCommondToOled(0xD9);
	IIC_SendCommondToOled(0xF1);
	IIC_SendCommondToOled(0xDB);
	IIC_SendCommondToOled(0x30);
	IIC_SendCommondToOled(0xA4);
	IIC_SendCommondToOled(0xA6);
	IIC_SendCommondToOled(0x8D);
	IIC_SendCommondToOled(0x14);
	IIC_SendCommondToOled(0xAF); // 开启显示
}



//点亮一整行(一页)函数---一页都写1----全亮了
uint8_t Oled_DisplayAline(void)
{
	//点亮第一页
	IIC_SendCommondToOled(0xB0);
	IIC_Start();
	
	IIC_SendByte(0x78);
	if(IICWaitAck())goto err;
	
	IIC_SendByte(0x40);//发送数据
	if(IICWaitAck())goto err;
	
	for(uint8_t i=0;i<128;i++)
	{
		IIC_SendByte(0xFF);//发送1
		if(IICWaitAck())goto err;
	}
	IIC_Stop();
	return 0;
	
	err:
	IIC_Stop();
	printf("IIC DisplayAline err\n");
	return 1;
}

//oled页写函数----将传入的数组，写一页
uint8_t OLED_WritePage(uint8_t *data,uint8_t count)
{
	if(count==0||count>128)return -1;
	IIC_Start();
	
	IIC_SendByte(0x78);
	if(IICWaitAck())goto err;
	
	IIC_SendByte(0X40);
	if(IICWaitAck())goto err;
	
	for(uint8_t j=0;j<count;j++)
	{
		IIC_SendByte(data[j]);
		if(IICWaitAck())goto err;
	}
	IIC_Stop();
	return 0;
	
	err:
	IIC_Stop();
	printf("[IIC] OLED_WritePage err");
	return -1;
}


//写哪一页函数
void OLED_SetP(uint8_t page,uint8_t x)
{
	IIC_SendCommondToOled(0xB0 | page);
	
	IIC_SendCommondToOled(0x10 | (x>>=4));//设置列的高位
	
	IIC_SendCommondToOled(0x00 | (x & 0x0F));//设置列的低位
}


//写整个屏幕函数
void OLED_ALLPage(void)
{
	uint8_t i;
	for(i=0;i<8;i++)
	{
		OLED_SetP(i,0);
		OLED_WritePage(oled_arr[i],128);
	}
}



//清屏函数---全写0
void OLED_Clear(void)
{
	uint8_t i,j;
	for(i=0;i<8;i++)
	{
		for(j=0;j<128;j++)
		{
			oled_arr[i][j] = 0x00;
		}
	}
	OLED_ALLPage();	
}