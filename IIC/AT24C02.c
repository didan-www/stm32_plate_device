#include "MyIIC.h"
#include "stm32f4xx.h"                  // Device header
#include "mydelay.h"
#include <stdio.h>
//写一个字节到AT24C02
uint8_t AT24C02_WriteByte(uint8_t add,uint8_t data)
{
	IIC_Start();
	IIC_SendByte(0xA0);
	MyDelay_ms(5);
	if(IICWaitAck())
		goto err;
		
	IIC_SendByte(add);
	MyDelay_ms(5);
	if(IICWaitAck())
		goto err;
	
	IIC_SendByte(data);
	MyDelay_ms(5);
	if(IICWaitAck())
		goto err;
	
	IIC_Stop();
	MyDelay_ms(5);
	return 0;
	err:
		IIC_Stop();
		printf("[IIC] err\n");
		return 1;
}


uint8_t AT24C02_ReadByte(uint8_t add)
{
		uint8_t data = 0;
	IIC_Start();
	
	IIC_SendByte(0xA0);
	if(IICWaitAck())
		goto err;
	
	IIC_SendByte(add);
	if(IICWaitAck())
		goto err;
	
	IIC_Start();
	
	IIC_SendByte(0xA1);
	if(IICWaitAck())
		goto err;
	
	data = IIC_ReadByte(1);
	IIC_Stop();
	return data;
	err:
		IIC_Stop();
		return -1;	
}

//跨页写函数(从mem开始，连续写入len个数据)
//void Write_Many(uint8_t mem,uint8_t *buf,uint16_t len)
//{
//	uint16_t offset=0;//记录buf数组写到第几个了
//	//先计算当前页还剩余几个字节可以存放
//	uint8_t put=8-(mem & (0x07));//当前页还能放几个
//	if(put>=len)
//	{
//		//把len个字节数据一个一个存入当前页
//		for(uint8_t i=0;i<len;i++)
//		{
//			AT24C02_WriteByte(mem,buf[offset]);
//			MyDelay_ms(5);
//			mem+=1;//地址向下一个存放地址偏移
//			offset++;
//		}
//	}
//	else
//	{
//		//第一步：先放put个位置
//		for(uint8_t i=0;i<put;i++)
//		{
//			AT24C02_WriteByte(mem,buf[offset]);
//			MyDelay_ms(5);
//			mem+=1;//地址向下一个存放地址偏移
//			offset++;
//		}
//		uint8_t a=(len-put)/8;//计算需要写几页
//		uint8_t b=(len-put)%8;
//		//第二部处理分页写的内容
//		while(a--)
//		{
//			Write_Many(mem,&buf[offset],8);
//			MyDelay_ms(5);
//			mem+=8;
//			offset+=8;
//		}
//		//第三步：处理剩余内容
//		if(b>0)
//		{
//			for(uint8_t i=0;i<b;i++)
//			{
//				AT24C02_WriteByte(mem,buf[offset]);
//				MyDelay_ms(5);
//				mem+=1;//地址向下一个存放地址偏移
//				offset++;
//			}
//		}
//	}
//}




void Write_Many(uint8_t mem,uint8_t *buf,uint16_t len)
{
//	uint16_t offset=0;//记录buf数组写到第几个了
//	//先计算当前页还剩余几个字节可以存放
//	uint8_t put=8-(mem & (0x07));//当前页还能放几个
//	if(put>=len)
//	{
//		//把len个字节数据一个一个存入当前页
//		for(uint8_t i=0;i<len;i++)
//		{
//			AT24C02_WriteByte(mem,buf[offset]);
//			MyDelay_ms(5);
//			mem++;//地址向下一个存放地址偏移
//			offset++;
//		}
//	}
//	else
//	{
//		for(uint8_t i=0;i<put;i++)
//		{
//			AT24C02_WriteByte(mem,buf[offset]);
//			MyDelay_ms(5);
//			mem+=1;//地址向下一个存放地址偏移
//			offset++;
//		}
//		uint16_t nextput=len-put;
//		for(uint8_t i=0;i<nextput;i++)
//		{
//			AT24C02_WriteByte(mem,buf[offset]);
//			MyDelay_ms(5);
//			mem+=1;//地址向下一个存放地址偏移
//			offset++;
//		}
//	}
	for(uint16_t i=0;i<len;i++)
	{
		AT24C02_WriteByte(mem++,buf[i]);
		MyDelay_ms(5);
	}
}


void Write_Many1(uint8_t mem,uint8_t *buf,uint16_t len)
{
	uint8_t put=(8-(mem & 0x07));
	uint8_t needput=(put>=len)?len:len-put;
	uint8_t nowneedput=0;//当前存了几个

		IIC_Start();
		IIC_SendByte(0xA0);
		if(IICWaitAck())
		goto err;
		
		IIC_SendByte(mem);
		if(IICWaitAck())
		goto err;
		while(nowneedput<put&& nowneedput < len)
		{	
			IIC_SendByte(buf[nowneedput]);
			nowneedput++;
			mem++;
			if(IICWaitAck())
			goto err;
		}
		IIC_Stop();
		MyDelay_ms(5);
		goto part2;
		
		err:
			IIC_Stop();
			printf("[IIC] err\n");
			return;
		
part2:
	uint16_t i=(len-nowneedput)/8;//循环次数
	while(i--)//每一页进来的循环
	{
		IIC_Start();
		IIC_SendByte(0xA0);
		if(IICWaitAck())
		goto err1;
		
		IIC_SendByte(mem);
		if(IICWaitAck())
		goto err1;
		
		for(uint8_t i=0;i<8;i++)
		{
			IIC_SendByte(buf[nowneedput]);
			nowneedput++;
			mem++;
			if(IICWaitAck())
			goto err1;
		}
		IIC_Stop();
		MyDelay_ms(5);
		continue;
		
		
		err1:
			IIC_Stop();
			printf("[IIC1] err\n");
		return;
	}
part3:
	//处理剩下的
	while(nowneedput<len)
	{
		IIC_Start();
		IIC_SendByte(0xA0);
		if(IICWaitAck())
		goto err2;
		
		IIC_SendByte(mem);
		if(IICWaitAck())
		goto err2;
		
		for(;nowneedput<len;nowneedput++)
		{
			IIC_SendByte(buf[nowneedput]);
			mem++;
			if(IICWaitAck())
			goto err2;
		}
		IIC_Stop();
		MyDelay_ms(5);
		return;
		err2:
			IIC_Stop();
			printf("[IIC2] err\n");
		return;
	}
}


void Write_Many2(uint8_t mem,uint8_t *buf,uint16_t len)
{
	uint16_t offset=0;//记录当前写入个数
	while(offset<len)
	{
		IIC_Start();
		IIC_SendByte(0xA0);
		if(IICWaitAck())
		goto err;
		
		IIC_SendByte(mem);
		if(IICWaitAck())
		goto err;
		
		while(mem%8!=0&&offset<len)
		{
			IIC_SendByte(buf[offset++]);
			mem++;
			if(IICWaitAck())
			goto err;
		}
		for(uint8_t i=0; i<8 && offset<len; i++)
		{
			IIC_SendByte(buf[offset++]);
			mem++;
			if(IICWaitAck())
				goto err;
		}
		IIC_Stop();
		MyDelay_ms(5);
	}
	return;
	err:
		IIC_Stop();
		printf("[IIC] err\n");
		return;
}