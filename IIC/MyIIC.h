#include "stm32f4xx.h"                  // Device header

#ifndef _MYIIC_H
#define MYSDA_HIGH()  GPIO_SetBits(GPIOB, GPIO_Pin_9)
#define MYSDA_LOW()   GPIO_ResetBits(GPIOB, GPIO_Pin_9) 
#define MYSCL_HIGH()  GPIO_SetBits(GPIOB, GPIO_Pin_8)
#define MYSCL_LOW()   GPIO_ResetBits(GPIOB, GPIO_Pin_8)


#define _MYIIC_H
//PB9--SDA----输入输出转换
//PB8--SCL-----输出模式


void My_IIC_GPIOInit(void);
void SDA_OUT(void);
void SDA_IN(void);
uint8_t SDA_READ(void);
void IIC_Start(void);
void IIC_SendByte(uint8_t data);
uint8_t IICWaitAck(void);
void IIC_Stop(void);
uint8_t IIC_ReadByte(uint8_t ack);
void IIC_ACK(void);
void IIC_NACK(void);
uint8_t AT24C02_WriteByte(uint8_t add,uint8_t data);
uint8_t AT24C02_ReadByte(uint8_t add);
void Write_Many(uint8_t mem,uint8_t *buf,uint16_t len);
void Write_Many1(uint8_t mem,uint8_t *buf,uint16_t len);
void Write_Many2(uint8_t mem,uint8_t *buf,uint16_t len);

#endif