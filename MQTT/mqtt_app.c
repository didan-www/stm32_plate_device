#include "stm32f4xx.h"                  // Device header
#include "mqtt_app.h"
#include "esp01.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <string.h>
#include "my_mqtt.h"
#include "device_config.h"
#define LOG_TAG "mqtt_app.c"
#define LOG_LVL ELOG_LVL_VERBOSE
#include "elog.h"

extern void    Usart2_TxInit(void);
extern void    Usart2_SetTxTask(TaskHandle_t handle);
extern uint8_t Usart2_SendBinary(uint8_t *data, uint16_t len, uint32_t timeout_ms);
extern MQTT_t mqtt;//来自my_mqtt.h固定报头结构体

/*宏定义*/
#define MQTT_BROKER_IP DEVICE_SERVER_IP
#define MQTT_BROKER_PORT DEVICE_MQTT_PORT
#define MQTT_KEEPALIVE 60 //broker如果60秒没收到任何数据，会主动断开（所有我们每30秒发一次心跳）
#define MQTT_CONNACK_TIMEOUT 5000 //ms（发了CONNECT后最多等5秒，broker正常应该击败毫秒就回）
#define MQTT_TCP_TIMEOUT 10000//TCP三次握手可能慢，给10秒
#define MQTT_SEND_TIMEOUT 5000  /*MQTT报文串口发送超时（MS）*/
/*全局变量*/
static MQTT_STATE_t mqtt_state=MQTT_STATE_DISCONNECTED;//当前状态，static限制只在本文件访问
static uint32_t last_ping_tick=0;//上次发数据/心跳的时间戳，心跳任务用它判断该不该发PING
static uint8_t mqtt_connack_code=0;//保存CONNECT返回码，连接失败打印日志用


volatile uint8_t g_net_online = 0;

/* 重连退避 */
static uint32_t reconnect_delay = 3000;  // 初始3秒
#define RECONNECT_MAX_DELAY 60000          // 最大60秒

/* 芯片 UID 基地址（STM32F407） */
#define STM32_UID_BASE 0x1FFF7A10

/* 用芯片 UID 生成 Client ID */
void mqtt_gen_client_id(char *buf, uint8_t buf_len)
{
    uint32_t uid0 = *(volatile uint32_t *)(STM32_UID_BASE);
    uint32_t uid1 = *(volatile uint32_t *)(STM32_UID_BASE + 4);
    uint32_t uid2 = *(volatile uint32_t *)(STM32_UID_BASE + 8);
    snprintf(buf, buf_len, "stm32_%08X%08X", uid0 ^ uid1, uid2);
}



/*组包函数：组包后通过USART2非阻塞发送*/
static uint8_t mqtt_send_packet(uint32_t timeout_ms)
{
	uint16_t pkt_len=mqtt.sendBuffPointNow-mqtt.sendBuff;
	if(pkt_len==0)return 0;
	/*把当前任务句柄告诉中断模块，发完后中断会通知当前任务*/
	Usart2_SetTxTask(xTaskGetCurrentTaskHandle());
	
	if(!Usart2_SendBinary(mqtt.sendBuff,pkt_len,timeout_ms))
	{
		log_e("send packet failed,len=%d",pkt_len);
		return 0;
	}
	return 1;
}

/*解析CONNACK报文:固定最少4字节：`0x20`(报文类型) | `0x02`(剩余长度) | 连接确认标志 | 返回码*/

static uint8_t parse_connack(void)
{
    uint8_t buf[8];
    uint16_t total = 0;
    uint32_t start = xTaskGetTickCount();

    /* ===== 第一步：在数据流里搜索 CONNACK 头 0x20 ===== */
    /* 跳过所有残留字节（\r\n、>、AT响应等），直到找到 0x20 */
    uint8_t ch;
    while ((xTaskGetTickCount() - start) < MQTT_CONNACK_TIMEOUT)
    {
        if (esp_rx_available() > 0)
        {
            esp_rx_read(&ch, 1);
            if (ch == 0x20)  /* 找到 CONNACK 报文头 */
            {
                buf[0] = 0x20;
                total = 1;
                break;
            }
            /* 不是 0x20，是残留数据，丢弃继续找 */
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (total == 0)
    {
        log_e("CONNACK header (0x20) not found in stream");
        return 0;
    }

    /* ===== 第二步：读剩余 3 字节 ===== */
    while (total < 4 && (xTaskGetTickCount() - start) < MQTT_CONNACK_TIMEOUT)
    {
        if (esp_rx_available() > 0)
        {
            uint16_t n = esp_rx_read(buf + total, 4 - total);
            total += n;
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (total < 4)
    {
        log_e("CONNACK timeout, got %d bytes", total);
        return 0;
    }

    /* buf[0]=0x20, buf[1]=0x02(剩余长度), buf[2]=确认标志, buf[3]=返回码 */
    mqtt_connack_code = buf[3];
    if (mqtt_connack_code != 0x00)
    {
        log_e("CONNACK refused, code=%d", mqtt_connack_code);
        return 0;
    }
    return 1;
}


//static uint8_t parse_connack(void)
//{
//	uint8_t buf[8];
//	uint16_t total=0;
//	uint32_t start=xTaskGetTickCount();//记录进入函数时刻的系统tick，用于超时判断
//	
//	/* CONNACK 固定4字节：0x20 + 0x02 + 确认标志 + 返回码 */
//	while (total < 4 && (xTaskGetTickCount() - start) < MQTT_CONNACK_TIMEOUT)
//    {
//        if (esp_rx_available() > 0)
//        {
//            uint16_t n = esp_rx_read(buf + total, 4 - total);
//            total += n;
//        }
//        else
//        {
//            vTaskDelay(pdMS_TO_TICKS(10));
//        }
//    }
//	if(total<4)
//	{
//		log_e("CONNACK timeout,got %d bytes",total);
//		return 0;
//	}
//	if(buf[0]!=0x20)
//	{
//		log_e("not CONNACK,first byte=0x%02x",buf[0]);
//		return 0;
//	}
//	mqtt_connack_code=buf[3];//buf[3]是返回码：0表示连接成功
//	if(mqtt_connack_code!=0x00)
//	{
//		log_e("CONNACK refused,code=%d",mqtt_connack_code);
//		return 0;
//	}
//	return 1;
//}

/*TCP连接成功后，清空接收缓冲区，防止旧数据干扰解析CONNACK报文*/
static void clear_rx_buffer(void)
{
	uint8_t dummy[64];
	while(esp_rx_available()>0)
	{
		esp_rx_read(dummy,sizeof(dummy));
	}
}


/*初始化*/
void MQTT_Init(void)
{
	Usart2_TxInit();
	MQTT_ResetState();
	log_i("MQTT app init done");
}

void MQTT_ResetState(void)
{
    mqtt_state = MQTT_STATE_DISCONNECTED;
    last_ping_tick = 0;
    mqtt_connack_code = 0;
}
/*连接broker*/
MQTT_Result_t MQTT_Connect(const char *client_id, uint16_t keepalive)
{
    if (mqtt_state == MQTT_STATE_CONNECTED)
        return MQTT_OK;

    if (!client_id || keepalive == 0)
        return MQTT_ERR_BAD_PARAM;
	
	clear_rx_buffer();
	
	/* ---- 第1步：建立 TCP 连接 + 透传模式 ---- */
    mqtt_state = MQTT_STATE_TCP_CONNECTING;
    clear_rx_buffer();

    if (!ESP01_ConnectTCP(MQTT_BROKER_IP, MQTT_BROKER_PORT, MQTT_TCP_TIMEOUT))
    {
        log_e("TCP connect failed");
        mqtt_state = MQTT_STATE_DISCONNECTED;
		g_net_online = 0;
        return MQTT_ERR_TCP_FAILED;
    }
	

    /* ---- 第2步：发 MQTT CONNECT 报文(带遗嘱) ---- */
    mqtt_state = MQTT_STATE_MQTT_CONNECTING;

    MQTTConnectStruct_t conn = {0};
    conn.clientID      = (uint8_t *)client_id;
    conn.keepAliveTime = keepalive;
    /* Persistent authenticated session. */
    conn.connectFlag   = 0x80 | 0x40;
    conn.willTopic     = NULL;
    conn.willMsg       = NULL;
    conn.userName      = (uint8_t *)DEVICE_MQTT_USERNAME;
    conn.password      = (uint8_t *)DEVICE_MQTT_PASSWORD;

    MQTTConnect(&conn);
    if (!mqtt_send_packet(MQTT_SEND_TIMEOUT))
    {
        mqtt_state = MQTT_STATE_ERROR;
		g_net_online = 0;
        return MQTT_ERR_SEND_FAILED;
    }
    log_i("CONNECT packet sent");

    /* ---- 第3步：等 CONNACK ---- */
    if (!parse_connack())
    {
        mqtt_state = MQTT_STATE_ERROR;
		g_net_online = 0;
        return (mqtt_connack_code != 0) ?
               MQTT_ERR_CONNACK_REFUSED : MQTT_ERR_CONNACK_TIMEOUT;
    }

    mqtt_state = MQTT_STATE_CONNECTED;
    last_ping_tick = xTaskGetTickCount();
	reconnect_delay = 3000;  /* 连接成功，重置退避时间 */
    log_i("MQTT connected!");
	g_net_online = 1;
    return MQTT_OK;
}

static uint8_t wait_suback(const uint16_t expected_message_id)
{
    uint8_t packet[5];
    uint16_t total = 0;
    uint32_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) < MQTT_CONNACK_TIMEOUT)
    {
        uint8_t value;
        if (esp_rx_available() == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        esp_rx_read(&value, 1);
        if (value == 0x90)
        {
            packet[0] = value;
            total = 1;
            break;
        }
    }

    while (total < sizeof(packet) &&
           (xTaskGetTickCount() - start) < MQTT_CONNACK_TIMEOUT)
    {
        if (esp_rx_available() > 0)
            total += esp_rx_read(packet + total, sizeof(packet) - total);
        else
            vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (total != sizeof(packet) || packet[1] != 0x03 ||
        (((uint16_t)packet[2] << 8) | packet[3]) != expected_message_id ||
        packet[4] == 0x80)
    {
        log_e("invalid SUBACK");
        return 0;
    }
    return 1;
}

MQTT_Result_t MQTT_Subscribe(const char *topic, uint8_t qos)
{
    static uint16_t next_message_id = 1;
    MQTTSubStruct_t subscription = {0};

    if (mqtt_state != MQTT_STATE_CONNECTED)
        return MQTT_ERR_NOT_CONNECTED;
    if (topic == NULL || qos != 1)
        return MQTT_ERR_BAD_PARAM;

    subscription.messageID = next_message_id++;
    if (next_message_id == 0)
        next_message_id = 1;
    subscription.payload = (uint8_t *)topic;
    subscription.QoS = qos;

    MQTTSub(&subscription);
    if (!mqtt_send_packet(MQTT_SEND_TIMEOUT))
        return MQTT_ERR_SEND_FAILED;
    if (!wait_suback(subscription.messageID))
        return MQTT_ERR_CONNACK_TIMEOUT;

    last_ping_tick = xTaskGetTickCount();
    log_i("MQTT subscribed: %s", topic);
    return MQTT_OK;
}

/*发布消息*/
MQTT_Result_t MQTT_Publish(const char *topic, const char *payload, uint8_t qos)
{
    if (mqtt_state != MQTT_STATE_CONNECTED)
        return MQTT_ERR_NOT_CONNECTED;

    if (!topic || !payload)
        return MQTT_ERR_BAD_PARAM;

    MQTTPublishStruct_t pub = {0};
    pub.topic     = (uint8_t *)topic;
    pub.payload   = (uint8_t *)payload;
    pub.Qos       = qos;
    pub.RETAN     = 0;
    pub.DUP       = 0;
    pub.messageID = 1;  /* QoS>0 时使用，QoS0 忽略 */

    MQTTPublish(&pub);
    if (!mqtt_send_packet(MQTT_SEND_TIMEOUT))
    {
        /* 发送失败，标记连接异常，上层可以触发重连 */
        mqtt_state = MQTT_STATE_ERROR;
        return MQTT_ERR_SEND_FAILED;
    }

    /* 发布数据也算通信，更新心跳计时 */
    last_ping_tick = xTaskGetTickCount();
    return MQTT_OK;
}

/*========== 心跳保活任务 ========== */
void MQTT_KeepAliveTask(void *arg)
{
    const uint32_t ping_interval = (MQTT_KEEPALIVE * 1000) / 2;  /* 保活周期的一半 */

    while (1)
    {
        if (mqtt_state == MQTT_STATE_CONNECTED)
        {
            uint32_t elapsed = xTaskGetTickCount() - last_ping_tick;
            if (elapsed >= ping_interval)
            {
                MQTTPing();
                if (mqtt_send_packet(MQTT_SEND_TIMEOUT))
                {
                    last_ping_tick = xTaskGetTickCount();
                    log_d("PING sent");
                }
                else
                {
                    log_e("PING failed, mark error");
                    mqtt_state = MQTT_STATE_ERROR;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ========== 处理 broker 下发的报文（预留） ========== */
void MQTT_ProcessRX(void)
{
    while (esp_rx_available() >= 2)
    {
        uint8_t hdr[2];
        uint16_t n = esp_rx_read(hdr, 2);
        if (n < 2) return;

        uint8_t msg_type = hdr[0] & 0xF0;
        uint8_t remain_len = hdr[1];

        switch (msg_type)
        {
        case 0xD0:  /* PINGRESP */
            log_d("PINGRESP");
            break;
        case 0x40:  /* PUBACK (QoS1) */
            log_d("PUBACK");
            /* 跳过剩余2字节（报文ID） */
            if (esp_rx_available() >= 2)
            {
                uint8_t dummy[2];
                esp_rx_read(dummy, 2);
            }
            break;
        case 0x30:  /* PUBLISH（broker 转发的订阅消息） */
        {
            /* 读主题长度 */
            if (esp_rx_available() >= 2)
            {
                uint8_t len_buf[2];
                esp_rx_read(len_buf, 2);
                uint16_t topic_len = (len_buf[0] << 8) | len_buf[1];

                /* 读主题 */
                if (esp_rx_available() >= topic_len)
                {
                    uint8_t topic[64];
                    esp_rx_read(topic, topic_len < 64 ? topic_len : 63);
                    topic[topic_len < 64 ? topic_len : 63] = '\0';

                    /* 剩余的是 payload */
                    uint16_t payload_len = remain_len - 2 - topic_len;
                    if (payload_len > 0 && esp_rx_available() >= payload_len)
                    {
                        uint8_t payload[128];
                        uint16_t rd = esp_rx_read(payload, payload_len < 128 ? payload_len : 127);
                        payload[rd] = '\0';
                        log_i("MQTT recv topic=%s payload=%s", topic, payload);
                        /* TODO: 在这里解析 payload，封装事件扔到 Event_Queue */
                    }
                }
            }
            break;
        }
        case 0x20:  /* CONNACK（连接阶段已处理，兜底） */
            if (esp_rx_available() >= 2)
            {
                uint8_t dummy[2];
                esp_rx_read(dummy, 2);
            }
            break;
        default:
            log_d("unknown MQTT msg: 0x%02X, len=%d", msg_type, remain_len);
            /* 跳过未知报文的剩余字节 */
            while (remain_len > 0 && esp_rx_available() > 0)
            {
                uint8_t dummy;
                esp_rx_read(&dummy, 1);
                remain_len--;
            }
            break;
        }
    }
}


/* ========== 状态查询 ========== */
MQTT_STATE_t MQTT_GetState(void)
{
    return mqtt_state;
}

uint8_t MQTT_IsConnected(void)
{
    return (mqtt_state == MQTT_STATE_CONNECTED) ? 1 : 0; 
}

void MQTT_Disconnect(void)
{
	if (mqtt_state != MQTT_STATE_CONNECTED)
    {
        mqtt_state = MQTT_STATE_DISCONNECTED;
        return;
    }

    /* 组 DISCONNECT 报文：0xE0 0x00 */
    MQTTInit();
    mqtt.messageType = MQTT_DISCONNECT;
    MakeMessageLength();

    mqtt_send_packet(MQTT_SEND_TIMEOUT);
    mqtt_state = MQTT_STATE_DISCONNECTED;
    log_i("MQTT disconnected");
}


uint32_t MQTT_GetReconnectDelay(void)
{
    uint32_t delay = reconnect_delay;
    /* 指数退避：每次翻倍，最大60秒 */
    reconnect_delay = (reconnect_delay * 2 < RECONNECT_MAX_DELAY) ?
                      reconnect_delay * 2 : RECONNECT_MAX_DELAY;
    return delay;
}
