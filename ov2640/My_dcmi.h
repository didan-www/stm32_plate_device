#ifndef __MY_DCMI_H__
#define __MY_DCMI_H__

#include "stm32f4xx.h"
#include <stdint.h>

/*===========================================================================
 * DCMI     
 *   D0 -- PC6    D4 -- PC11    PCLK  -- PA6
 *   D1 -- PC7    D5 -- PB6     HSYNC -- PA4
 *   D2 -- PE0    D6 -- PE5     VSYNC -- PB7
 *   D3 -- PE1    D7 -- PE6
 *
 * DMA: DMA2 Stream1 Channel1  DCMI ֻ     Stream1    Stream7    Ch1  
 *===========================================================================*/

/* DMA ˫     л  ص  */

extern volatile uint8_t g_dcmi_capturing;
extern void (*dcmi_rx_callback)(void);

void My_DCMI_Init(void);

void DCMI_DMA_Init(uint32_t mem0addr, uint32_t mem1addr,
                   uint16_t memsize,
                   uint32_t memblen, uint32_t meminc);

void DCMI_Start(void);
void DCMI_Stop(void);

#endif /* __MY_DCMI_H__ */
