#ifndef _MYTIME_H_
#define _MYTIME_H_
#include "stm32f4xx.h"

void Systick_Init(void);

void Mytime6_Init(void);
void Srf05_Init(void);
void Srf05_Start(void);
float Srf05_ReadDistance();

void MyDelay_us(uint32_t num);


void MyDelay_ms(uint32_t num);


void MyDelay_s(uint32_t num);

#endif