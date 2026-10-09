 #ifndef __MYUSART1_H__
#define __MYUSART1_H__
#include "stm32f4xx.h"  // Device header


void MyUsart1Init(void);
void Usart1_SendString(uint8_t *string);
uint8_t USART1_ReceiveByte(uint8_t *data,uint32_t timeout);
uint16_t USART1_ReadFrame(uint8_t*buf,uint16_t data_len);
#endif