#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "myusart1.h"
#include <stdio.h>
#include "elog.h"
#include "app.h"
#include "mydelay.h"
#include "esp01.h"
uint8_t oled_arr[8][128];


volatile uint32_t g_hardfault_flag = 0;
/* 覆盖启动文件里的弱定义 HardFault_Handler */
void HardFault_Handler(void)
{
    /* 卡死后在调试器里看这个变量，如果是 0xAA 就是 HardFault */
    g_hardfault_flag = 0xAA;
    
    /* 防止编译器优化掉 */
    while(g_hardfault_flag != 0)
    {
        /* 死循环 */
    }
}


int main(void)
{
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
	Delay_Init();
    MyUsart1Init();
    printf("\r\n\r\n--- System Boot ---\r\n");

    elog_init();
    elog_set_fmt(ELOG_LVL_ASSERT,  ELOG_FMT_ALL);
    elog_set_fmt(ELOG_LVL_ERROR,   ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME | ELOG_FMT_T_INFO | ELOG_FMT_FUNC | ELOG_FMT_LINE);
    elog_set_fmt(ELOG_LVL_WARN,    ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME | ELOG_FMT_T_INFO);
    elog_set_fmt(ELOG_LVL_INFO,    ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME | ELOG_FMT_T_INFO);
    elog_set_fmt(ELOG_LVL_DEBUG,   ELOG_FMT_ALL & ~ELOG_FMT_FUNC);
    elog_set_fmt(ELOG_LVL_VERBOSE, ELOG_FMT_ALL & ~ELOG_FMT_FUNC);
    elog_start();

    elog_i("main", "System Boot");

    xTaskCreate(Start_Task, "Start", 256, NULL, 1, &StartTask_TaskHandle);

    vTaskStartScheduler();

    while (1) {}
}