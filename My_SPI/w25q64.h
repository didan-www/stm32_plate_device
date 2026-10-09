#ifndef _W25Q64_HARD_H
#define _W25Q64_HARD_H

#include "stm32f4xx.h"                  // Device header
#include "my_spi.h"


#define W25Q64_ID        0XEF16
#define W25Q64_SECTOR_SIZE 4096U

void W25Q64_Init(void);
uint32_t W25Q64_ReadID(void);
void W25Q64_Read(uint8_t *buf,uint32_t addr,uint32_t len);
void W25Q64_Write(uint8_t *buf,uint32_t addr,uint32_t len);
void W25Q64_SectorErase(uint32_t addr);
void W25Q64_ChipErase(void);   //整片擦除，你代码需要


#endif