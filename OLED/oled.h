#ifndef _OLED_H_
#define _OLED_H_
#include "stm32f4xx.h"                  // Device header


uint8_t IIC_SendCommondToOled(uint8_t Commond);
void OLED_Init(void);
uint8_t Oled_DisplayAline(void);
uint8_t OLED_WritePage(uint8_t *data,uint8_t count);
void OLED_ALLPage(void);
void OLED_Clear(void);




#endif