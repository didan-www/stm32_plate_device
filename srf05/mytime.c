#include "mytime.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include "stm32f4xx.h"
#include "mydelay.h"
#include "elog.h"
#include <string.h>

#define LOG_TAG "mytime.c"
#define LOG_LVL ELOG_LVL_VERBOSE
#include "elog.h"

SemaphoreHandle_t xSrfSem = NULL;
volatile uint32_t srf_width;

static void Mytime6_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);

    TIM_TimeBaseInitTypeDef S1;
    memset(&S1, 0, sizeof(S1));
    S1.TIM_Period = 65536 - 1;
    S1.TIM_Prescaler = 84 - 1;
    S1.TIM_ClockDivision = TIM_CKD_DIV1;
    S1.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM6, &S1);
    TIM_Cmd(TIM6, DISABLE);
}

void Srf05_Init(void)
{
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC| RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

    GPIO_InitTypeDef S1;
    S1.GPIO_Mode = GPIO_Mode_OUT;
    S1.GPIO_OType = GPIO_OType_PP;
    S1.GPIO_Pin = GPIO_Pin_1;
    S1.GPIO_Speed = GPIO_High_Speed;
    GPIO_Init(GPIOC, &S1);
    GPIO_ResetBits(GPIOC, GPIO_Pin_1);

    GPIO_InitTypeDef S;
    S.GPIO_Mode = GPIO_Mode_IN;
    S.GPIO_PuPd = GPIO_PuPd_DOWN;
    S.GPIO_Pin = GPIO_Pin_0;
    GPIO_Init(GPIOB, &S);

    SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, EXTI_PinSource0);

    EXTI_InitTypeDef S2;
    S2.EXTI_Line = EXTI_Line0;
    S2.EXTI_LineCmd = ENABLE;
    S2.EXTI_Mode = EXTI_Mode_Interrupt;
    S2.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
    EXTI_Init(&S2);

    NVIC_InitTypeDef S3;
    S3.NVIC_IRQChannel = EXTI0_IRQn;
    S3.NVIC_IRQChannelCmd = ENABLE;
    S3.NVIC_IRQChannelPreemptionPriority = 7;
    S3.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&S3);

    Mytime6_Init();

    xSrfSem = xSemaphoreCreateBinary();
    srf_width = 0;
}

static void Srf05_Start(void)
{
    EXTI_ClearITPendingBit(EXTI_Line0);
    TIM_Cmd(TIM6, DISABLE);
    TIM_SetCounter(TIM6, 0);
    GPIO_SetBits(GPIOC, GPIO_Pin_1);
    Delay_us(12);
    GPIO_ResetBits(GPIOC, GPIO_Pin_1);
}

void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) == SET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0) == 1)
        {
            TIM_SetCounter(TIM6, 0);
            TIM_Cmd(TIM6, ENABLE);
        }
        else
        {
            TIM_Cmd(TIM6, DISABLE);
            srf_width = TIM_GetCounter(TIM6);
            xSemaphoreGiveFromISR(xSrfSem, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
}

float Srf05_ReadDistance(void)
{
    float dist_cm = -1.0f;
    /* 清除上一次未消费的完成信号，保证本次等待对应本次触发。 */
    if(xSrfSem != NULL)
        (void)xSemaphoreTake(xSrfSem, 0);
    Srf05_Start();
    if (xSemaphoreTake(xSrfSem, pdMS_TO_TICKS(35)) == pdTRUE)
    {
        dist_cm = srf_width * 0.017f;
        /* SRF05 典型量程约为 2~400 cm，超出范围视为无效回波。 */
        if(dist_cm < 2.0f || dist_cm > 400.0f)
            dist_cm = -1.0f;
    }
    return dist_cm;
}
