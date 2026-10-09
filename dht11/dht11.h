#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f4xx.h"

typedef struct {
    uint8_t humidity_int;
    uint8_t humidity_dec;
    uint8_t temp_int;
    uint8_t temp_dec;
    uint8_t checksum;
} DHT11_Data_t;

void DHT11_Init(void);
uint8_t DHT11_Read(DHT11_Data_t *data);

#endif
