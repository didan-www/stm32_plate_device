#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "test.h"
#include "dht11.h"
#include "mytime.h"
#include "MyIIC.h"
#include "oled.h"

#define LOG_TAG "tasks.c"
#define LOG_LVL ELOG_LVL_VERBOSE
#include "elog.h"

extern uint8_t oled_arr[8][128];

void elog_test_task(void *pvParameters)
{
    uint8_t buf[16] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                       0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F};
    int cnt = 0;

    log_a("assert test");
    log_e("error test, code=%d", -1);
    log_w("warn test, temp=%.1f", 25.5);
    log_i("info test, cnt=%d", cnt);
    log_d("debug test, ptr=%p", buf);
    log_v("verbose test");

    elog_hexdump("buf", 16, buf, sizeof(buf));

    while (1)
    {
        cnt++;
        log_i("running, cnt=%d", cnt);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void test_task(void *pvParameters)
{
    printf("\r\n========== FreeRTOS Test Task Start ==========\r\n");
    printf("SystemCoreClock = %u Hz\r\n", SystemCoreClock);

    Test_SPI_Flash();
    Test_W25Q64();
    Test_FatFs();

    printf("\r\n========== All Tests Done ==========\r\n");

    while (1)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

void dht11_task(void *pvParameters)
{
    DHT11_Data_t d;
    DHT11_Init();

    while (1)
    {
        if (DHT11_Read(&d) == 0)
            elog_i("dht11", "Temp:%d.%dC Humi:%d.%d%%",
                   d.temp_int, d.temp_dec, d.humidity_int, d.humidity_dec);
        else
            elog_e("dht11", "read fail");

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void oled_task(void *pvParameters)
{
    My_IIC_GPIOInit();
    OLED_Init();
    OLED_Clear();

    Oled_DisplayAline();

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void sr04_task(void *pvParameters)
{
    float dist;

    Srf05_Init();

    while (1)
    {
        dist = Srf05_ReadDistance();

        if (dist >= 0)
            elog_i("sr04", "dist=%.1f cm", dist);
        else
            elog_e("sr04", "read timeout");

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}