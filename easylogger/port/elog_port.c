#include <elog.h>
#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include <stdio.h>

static SemaphoreHandle_t xOutputMutex = NULL;

ElogErrCode elog_port_init(void) {
    xOutputMutex = xSemaphoreCreateMutex();
    return ELOG_NO_ERR;
}

void elog_port_deinit(void) {
    if (xOutputMutex) {
        vSemaphoreDelete(xOutputMutex);
        xOutputMutex = NULL;
    }
}

void elog_port_output(const char *log, size_t size) {
    for (size_t i = 0; i < size; i++) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
        USART_SendData(USART1, (uint8_t)log[i]);
    }
}

void elog_port_output_lock(void) {
    if (xOutputMutex && xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        xSemaphoreTake(xOutputMutex, portMAX_DELAY);
}

void elog_port_output_unlock(void) {
    if (xOutputMutex && xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        xSemaphoreGive(xOutputMutex);
}

const char *elog_port_get_time(void) {
    static char time_str[16];
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        uint32_t tick = xTaskGetTickCount();
        uint32_t sec = tick / configTICK_RATE_HZ;
        uint32_t ms = tick % configTICK_RATE_HZ;
        snprintf(time_str, sizeof(time_str), "%lu.%03lu", sec, ms);
    } else {
        snprintf(time_str, sizeof(time_str), "0.000");
    }
    return time_str;
}

const char *elog_port_get_p_info(void) {
    return "mcu";
}

const char *elog_port_get_t_info(void) {
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return pcTaskGetName(NULL);
    return "main";
}
