#include "dht11.h"
#include "mydelay.h"
#include "FreeRTOS.h"
#include "task.h"

#define DHT11_PORT    GPIOE
#define DHT11_PIN     GPIO_Pin_9

static void gpio_out(void)
{
    GPIO_InitTypeDef g;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
    g.GPIO_Mode  = GPIO_Mode_OUT;
    g.GPIO_Pin   = DHT11_PIN;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_Speed = GPIO_Speed_100MHz;
    g.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(DHT11_PORT, &g);
}

static void gpio_in(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Mode  = GPIO_Mode_IN;
    g.GPIO_Pin   = DHT11_PIN;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_Speed = GPIO_Speed_100MHz;
    g.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(DHT11_PORT, &g);
}

static uint8_t wait_level(uint8_t level, uint32_t timeout_us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = timeout_us * (SystemCoreClock / 1000000U);
    while (GPIO_ReadInputDataBit(DHT11_PORT, DHT11_PIN) != level)
    {
        if ((DWT->CYCCNT - start) > ticks)
            return 1;
    }
    return 0;
}

void DHT11_Init(void)
{
    Delay_Init();
    gpio_out();
    GPIO_SetBits(DHT11_PORT, DHT11_PIN);
}

uint8_t DHT11_Read(DHT11_Data_t *data)
{
    uint8_t i;
    uint8_t bytes[5] = {0};

    if (data == NULL)
        return 2;

    gpio_out();
    GPIO_ResetBits(DHT11_PORT, DHT11_PIN);
    vTaskDelay(pdMS_TO_TICKS(20));

    taskENTER_CRITICAL();

    GPIO_SetBits(DHT11_PORT, DHT11_PIN);
    Delay_us(30);
    gpio_in();

    if (wait_level(0, 100)) goto fail;
    if (wait_level(1, 100)) goto fail;
    if (wait_level(0, 100)) goto fail;

    for (i = 0; i < 40; i++)
    {
        if (wait_level(1, 100)) goto fail;
        Delay_us(40);
        if (GPIO_ReadInputDataBit(DHT11_PORT, DHT11_PIN) == 1)
            bytes[i / 8] |= (1 << (7 - (i % 8)));
        if (wait_level(0, 100)) goto fail;
    }

    taskEXIT_CRITICAL();
    gpio_out();
    GPIO_SetBits(DHT11_PORT, DHT11_PIN);

    if ((uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]) != bytes[4])
        return 3;

    data->humidity_int = bytes[0];
    data->humidity_dec = bytes[1];
    data->temp_int     = bytes[2];
    data->temp_dec     = bytes[3];
    data->checksum     = bytes[4];
    return 0;

fail:
    taskEXIT_CRITICAL();
    gpio_out();
    GPIO_SetBits(DHT11_PORT, DHT11_PIN);
    return 1;
}
