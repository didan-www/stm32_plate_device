#ifndef _APP_TEST_H
#define _APP_TEST_H

#include "stm32f4xx.h"

void SystemClock_Config(void);
void Test_SPI_Flash(void);
void Test_W25Q64(void);
uint8_t Test_FatFs(void);

#endif
