#ifndef __SSD1306_H__
#define __SSD1306_H__

#include "stm32f4xx.h"

#define OLED_ADDR   0x78   
#define OLED_W      128
#define OLED_H      64
#define OLED_PAGES  8      

void OLED_Init(void);
void OLED_Clear(void);
void OLED_Display(void);                              /* °ÑÖ¡»º³åË¢µ½ÆÁÄ» */
void OLED_DrawPixel(uint8_t x, uint8_t y, uint8_t c);
void OLED_DrawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);
void OLED_DrawString(uint8_t x, uint8_t y, const char *str); /* 6x8 ×ÖÌå */
void OLED_DrawChar(uint8_t x, uint8_t y, char ch);

#endif