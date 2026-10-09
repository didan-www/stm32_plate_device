#ifndef _TASKS_H_
#define _TASKS_H_

#include "stm32f4xx.h"

void elog_test_task(void *pvParameters);
void test_task(void *pvParameters);
void dht11_task(void *pvParameters);
void oled_task(void *pvParameters);
void sr04_task(void *pvParameters);

#endif
