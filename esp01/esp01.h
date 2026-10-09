#ifndef _ESP01_H
#define _ESP01_H

#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*esp01相关实现API*/
void     Esp01_Usart2Init(void);
void     Usart2_SendString(uint8_t *str);
uint8_t  ESP01_SendCmd(char *cmd, char *ack, uint16_t waittime);
void     Esp01_Init(void);
uint16_t esp_rx_available(void);
uint16_t esp_rx_read(uint8_t *dst, uint16_t max_len);
uint8_t ESP01_SendData(uint8_t *data, uint16_t len);


uint8_t ESP01_ConnectTCP(const char*ip,uint16_t port,uint32_t timeout_ms);
uint8_t ESP01_IsTCPConnected(void);

/*串口2发送接收MQTT相关API*/
void Usart2_TxInit(void);
void Usart2_SetTxTask(TaskHandle_t handle);
uint8_t Usart2_SendBinary(uint8_t*data,uint16_t len,uint32_t timeout_ma);

#endif
