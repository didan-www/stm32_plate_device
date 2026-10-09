#ifndef _MY_SPI_HARD_H_
#define _MY_SPI_HARD_H_
#include "stm32f4xx.h"                  // Device header

#ifndef __MY_SPI_H
#define __MY_SPI_H

#include "stm32f4xx.h"

//引脚与外设宏定义 SPI2 PB12?PB15
#define SPI_GPIO_PORT       GPIOB
#define SPI_PIN_CS          GPIO_Pin_12
#define SPI_PIN_SCK         GPIO_Pin_13
#define SPI_PIN_MISO        GPIO_Pin_14
#define SPI_PIN_MOSI        GPIO_Pin_15

#define SPI_SRC_SCK         GPIO_PinSource13
#define SPI_SRC_MISO        GPIO_PinSource14
#define SPI_SRC_MOSI        GPIO_PinSource15

#define SPI_AF              GPIO_AF_SPI2
#define SPIx                SPI2
#define SPI_GPIO_CLK        RCC_AHB1Periph_GPIOB
#define SPIx_CLK            RCC_APB1Periph_SPI2


void MySPI_Init(void);
void MySPI_Start(void);
void MySPI_Stop(void);
uint8_t MySPI_SwapByte(uint8_t ByteSend);
void MySPI_SendBuf(const uint8_t *buf, uint32_t len);
void MySPI_RecvBuf(uint8_t *buf, uint32_t len);

#if (configUSE_FREERTOS == 1)
#include "FreeRTOS.h"
#include "semphr.h"
extern SemaphoreHandle_t spi_mutex;
void SPI_Lock(void);
void SPI_Unlock(void);
#endif

#endif

#endif

