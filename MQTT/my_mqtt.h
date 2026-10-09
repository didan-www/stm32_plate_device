#ifndef __MY_MQTT_H__
#define __MY_MQTT_H__
#include "stm32f4xx.h"                  // Device header

//定义消息体大小
#define BUFF_SIZE 512
#define MQTT_KEEP_ALIVE 300U  //300s

/***************************
    定义数据类型
    @整型
    @字符型
    注意：大小端的问题
****************************/
typedef enum
{
    MQTT_INT = -1, //从数据的高位向数据的地位遍历
    MQTT_CHAR = 1,
}DataType_t;

//定义报文类型
typedef enum
{
    MQTT_CONNECT = 0x10U,    //连接
    MQTT_CONNACK = 0x20U,   //连接响应
    MQTT_PUBLISH = 0x30U,   //发布消息
    MQTT_PUBACK = 0x40U,    //发布消息响应 QoS = 1
    MQTT_PUBREC = 0x50U,    //发布收到
    MQTT_PUBREL = 0x60U,    //发布释放
    MQTT_PUBCOMP = 0x70U,   //Qos2消息发布完成
    MQTT_SUBSCRIBE = 0x82U, //订阅
    MQTT_SUBACK = 0x90U,    //订阅响应
    MQTT_UNSUBSCRIBE = 0xA2U,   //取消订阅
    MQTT_UNSUBACK = 0xB0U,  //取消订阅响应
    MQTT_PINGREQ = 0xC0U,   //保活
    MQTT_PINGRESP = 0xD0U,  //保活响应
    MQTT_DISCONNECT = 0xE0U //断开连接
}MessageType_t;

/*
    报文组成：固定报头 + 可变报头 + 有效负载
*/
//固定报头
typedef struct
{
    MessageType_t messageType; //报文类型
    uint8_t *sendBuff;          //  发送缓冲区
    uint8_t *sendBuffPointNow;  //指向发送缓冲区的当前位置
    uint32_t remainLength;  //剩余长度
}MQTT_t;
//连接报文
typedef struct
{
    uint8_t connectFlag; //连接标志
    uint16_t keepAliveTime; //保活时间
    uint8_t *clientID;  //客户端ID
    uint8_t *userName;  //用户名
    uint8_t *password;  //  密码
	uint8_t *willTopic;   /* ← 新增：遗嘱主题 */
    uint8_t *willMsg;     /* ← 新增：遗嘱消息 */
}MQTTConnectStruct_t;
//订阅报文
typedef struct
{
    uint16_t messageID; //报文标识符
    uint8_t *payload; //有效载荷
    uint8_t QoS; //服务质量
}MQTTSubStruct_t;

//发布报文
typedef struct
{
    uint8_t RETAN;
    uint8_t Qos;
    uint8_t DUP;//重发标志
    uint16_t messageID; //报文标志符
    uint8_t * topic; //主题
    uint8_t * payload;//有效载荷
    
    
}MQTTPublishStruct_t;

void MQTTInit(void);
void MQTTConnect(MQTTConnectStruct_t *s1);
void MQTTSub(MQTTSubStruct_t * s1);
void MQTTPublish(MQTTPublishStruct_t *s1);
void MQTTPing(void);
uint32_t DecodeMessageLength(char* data);//解析解码

#endif



