#include "stm32f4xx.h"                  // Device header
#include "my_mqtt.h"
#include "stdio.h"
#include "string.h"

MQTT_t mqtt = {0};//保存报文类型，两个关键指针
uint8_t MQTTSendbuff[BUFF_SIZE] = {0}; //报文缓冲区

/**
    @brief 组包函数
    @param 有效数据
    @param 数据长度字节数
    @param 数据类型： 整数 还是字符串
*/
void MQTTSendDataToBuff(void *data,uint32_t n,DataType_t dataType)
{
    if(data == NULL)
        return;
	
	/* 溢出检查：防止写超过缓冲区 */
    if ((uint32_t)(mqtt.sendBuffPointNow - MQTTSendbuff) + n > BUFF_SIZE)
    {
        return;  // 空间不足，丢弃
    }
	
    if(dataType == MQTT_INT)
        data = (uint8_t *)data + n - 1; 
    while(n--) {
        *mqtt.sendBuffPointNow++ = *(uint8_t *)data;
        data = (uint8_t *)data + dataType;
    }
}

/*
    编码剩余长度，并向发送buff中写入报文类型
*/
void MakeMessageLength(void)
{
    uint8_t demp[4] = {0};
    uint8_t length = 0;
    uint8_t Byte = 0;
    uint32_t data = 0; //字节长度
    
    data = mqtt.sendBuffPointNow - mqtt.sendBuff - 1;
    do
    {
        Byte = data % 128;
        data = data / 128;
        if(data > 0) {
            Byte = Byte | 128;
        }
        demp[length] = Byte;
        length ++;
    }while(data > 0);
    
    //将剩余长度写入buffer
    while(length --)
    {
        *mqtt.sendBuff = demp[length];
        mqtt.sendBuff--;
    }
    //写入报文的类型
    *mqtt.sendBuff = mqtt.messageType;
}

/* 解码剩余长度 */
uint32_t DecodeMessageLength(char* data)
{
    unsigned int m = 1;
    unsigned int value = 0;
    unsigned char Byte = 0;
    do
    {
        Byte = *data++;
        value += (Byte & 0x7F) * m;
        m *= 128;
    } while ((Byte & 0x80) != 0);
//    printf("remainlentht: %d\n",value);
    return value;
}

/*
    初始化mqtt
    MQTTSendbuff:缓冲区的首地址
*/
void MQTTInit(void)
{
    mqtt.messageType = 0;  //清空报文类型
    mqtt.sendBuff = MQTTSendbuff + 4;  //固定报头
    mqtt.sendBuffPointNow = MQTTSendbuff + 5;  //从可变报头写
}

/*
    mqtt连接
*/
void MQTTConnect(MQTTConnectStruct_t *s1)
{
    uint16_t t = 0;
    uint8_t protocolLevel = 0x04; //MQTT3.1.1版本号
    
    MQTTInit();
    mqtt.messageType = MQTT_CONNECT;
    
    /***********************封装可变报头******************************************/
    t = 0x4; // 协议名长度
    MQTTSendDataToBuff(&t,2,MQTT_INT);
    MQTTSendDataToBuff("MQTT",4,MQTT_CHAR);
    
    //协议级别 0x04
    MQTTSendDataToBuff(&protocolLevel,1,MQTT_INT); 
    
    //连接标志
    MQTTSendDataToBuff(&s1->connectFlag,1,MQTT_INT);
    
    //保持连接，消息之间最大的等待时间
    MQTTSendDataToBuff(&s1->keepAliveTime,2,MQTT_INT);
	
	
	
	/* ===== 新增：遗嘱消息（LWT），connectFlag bit5 置位时才加 ===== */
    if ((s1->connectFlag & 0x04) && s1->willTopic && s1->willMsg)
    {
        /* 遗嘱主题 */
        t = strlen((char*)s1->willTopic);
        MQTTSendDataToBuff(&t, 2, MQTT_INT);
        MQTTSendDataToBuff((void*)s1->willTopic, t, MQTT_CHAR);

        /* 遗嘱消息内容 */
        t = strlen((char*)s1->willMsg);
        MQTTSendDataToBuff(&t, 2, MQTT_INT);
        MQTTSendDataToBuff((void*)s1->willMsg, t, MQTT_CHAR);
    }
    
    /***********************封装有效载荷******************************************/
    //添加客户端标识符：长度 + 数据
    t = strlen((char*)s1->clientID);
    MQTTSendDataToBuff(&t, 2, MQTT_INT);  
    MQTTSendDataToBuff((void *)s1->clientID, t, MQTT_CHAR); 
    
    //添加用户名：长度 +数据
	/*用户名：只有connectFlag bit7置位且userName非空才添加*/
	if((s1->connectFlag&0x80)&&s1->userName)
	{
		t = strlen((char*)s1->userName);
		MQTTSendDataToBuff(&t, 2, MQTT_INT);
		MQTTSendDataToBuff((void *)s1->userName, t, MQTT_CHAR);
	}
    
    //添加密码：长度 + 数据
	/*密码：只有commectFlag bit6置位且userName非空才添加*/
	if((s1->connectFlag&0x40)&&s1->password)
	{
		t = strlen((char*)s1->password);
		MQTTSendDataToBuff(&t, 2, MQTT_INT);
		MQTTSendDataToBuff((void *)s1->password, t, MQTT_CHAR);
	}
    
    /*编码剩余长度：协议类型 + 剩余长度*/
    MakeMessageLength();
    //Usart2_MqttSend(mqtt.sendBuff, mqtt.sendBuffPointNow - mqtt.sendBuff);
    
    //printf("MQTT CONNECT Packet Sent\n");  
}

/* 订阅主题 
*/
void MQTTSub(MQTTSubStruct_t * s1) 
{
    uint16_t temp = 0;
    MQTTInit();
    
    //报文的类型
    mqtt.messageType = MQTT_SUBSCRIBE;
    
    /*可变报头*/
    //报文标识符,16位非零
    MQTTSendDataToBuff(&s1->messageID, 2, MQTT_INT);
    
    /*有效载荷*/
    temp = strlen((char*)s1->payload);
    MQTTSendDataToBuff(&temp, 2, MQTT_INT);
    MQTTSendDataToBuff((void *)s1->payload, temp, MQTT_CHAR);
    
    /*服务质量*/
    MQTTSendDataToBuff(&s1->QoS, 1, MQTT_INT);
    
    /*编码剩余长度*/
    MakeMessageLength();
    
    /*发送服务器*/
    //Usart2_MqttSend(mqtt.sendBuff, mqtt.sendBuffPointNow - mqtt.sendBuff);
    
}

/*
    发布消息
*/
void MQTTPublish(MQTTPublishStruct_t *s1)
{
    uint16_t temp = 0;
    MQTTInit();
    
    //报文的类型
    mqtt.messageType = MQTT_PUBLISH;
    mqtt.messageType |= s1->RETAN;
    mqtt.messageType |= s1->Qos << 1;
    mqtt.messageType |= s1->DUP << 3;
    
    /*可变报头: 主题长度（两个字节）+主题名+报文标识符长度+报文标识符*/
    //主题长度
    temp = strlen((char*)s1->topic);
    MQTTSendDataToBuff(&temp, 2, MQTT_INT);
    //主题
    MQTTSendDataToBuff((void *)s1->topic, temp, MQTT_CHAR);
    
    if(s1->Qos > 0)
    {
        MQTTSendDataToBuff(&s1->messageID, 2, MQTT_INT);
    }
    
    //有效载荷
    temp = strlen((char*)s1->payload);
    MQTTSendDataToBuff((void *)s1->payload, temp, MQTT_CHAR);
    
    /*编码剩余长度*/
    MakeMessageLength();
    uint16_t pkt_len = mqtt.sendBuffPointNow - mqtt.sendBuff;
    //log_i("[Publish] packet len = %d", pkt_len);
    
    //Usart2_MqttSend(mqtt.sendBuff, mqtt.sendBuffPointNow - mqtt.sendBuff);
    //log_i("[Publish] send done"); 
}

/*心跳检测*/
void MQTTPing(void)
{
    unsigned short temp = 0;
    
    MQTTInit();
    //报文类型
    mqtt.messageType = MQTT_PINGREQ;
    /*编码剩余长度*/
    MakeMessageLength();
    /*发送服务器*/
    //Usart2_MqttSend(mqtt.sendBuff,mqtt.sendBuffPointNow - mqtt.sendBuff);
}

