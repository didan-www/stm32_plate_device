#ifndef _MQTT_APP_H_
#define _MQTT_APP_H_
#include "stm32f4xx.h"                  // Device header

extern volatile uint8_t g_net_online;


/*MQTT状态枚举*/
typedef enum
{
	MQTT_STATE_DISCONNECTED=0,//未连接
	MQTT_STATE_TCP_CONNECTING,//正在建TCP
	MQTT_STATE_MQTT_CONNECTING,//TCP已通，正在发CONNECT等CONNACK
	MQTT_STATE_CONNECTED,//已连接，可以发数据
	MQTT_STATE_ERROR//出错了
}MQTT_STATE_t;



/*错误码枚举*/
typedef enum
{
	MQTT_OK=0,
	MQTT_ERR_TCP_FAILED,//TCP连不上
	MQTT_ERR_CONNACK_TIMEOUT,//发了CONNECT没收到CONNACK
	MQTT_ERR_CONNACK_REFUSED,//broker拒绝连接（返回码非0）
	MQTT_ERR_NOT_CONNECTED,//没连接就想发数据
	MQTT_ERR_SEND_FAILED,
	MQTT_ERR_BAD_PARAM//参数为NULL
}MQTT_Result_t;

void           MQTT_Init(void);
MQTT_Result_t  MQTT_Connect(const char *client_id, uint16_t keepalive);
MQTT_Result_t  MQTT_Subscribe(const char *topic, uint8_t qos);
MQTT_Result_t  MQTT_Publish(const char *topic, const char *payload, uint8_t qos);
void           MQTT_KeepAliveTask(void *arg);
void           MQTT_ProcessRX(void);
MQTT_STATE_t   MQTT_GetState(void);
uint8_t        MQTT_IsConnected(void);

void     MQTT_Disconnect(void);
void     MQTT_ResetState(void);
uint32_t MQTT_GetReconnectDelay(void);
void mqtt_gen_client_id(char *buf, uint8_t buf_len);

#endif
