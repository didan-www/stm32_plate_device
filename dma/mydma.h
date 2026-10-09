#ifndef __MYDMA_H__
#define __MYDMA_H__

void Usart1_DMA_Init(void);
void DMA2_Stream7_Chan4_Init(void);
void DMA2_Stream1_Chan4_Init(void);
void Usart1_DMA_Send(const uint8_t *buf, uint16_t len);

#endif

