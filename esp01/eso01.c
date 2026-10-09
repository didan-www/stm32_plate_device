#include "stm32f4xx.h"                  // Device header
#include "esp01.h"
#include "string.h"
#include "string.h"
#include "FreeRTOS.h"
#include "semphr.h"
#define LOG_TAG "esp01.c"
#define LOG_LVL ELOG_LVL_VERBOSE
#include "elog.h"

/*Usart2发送相关（任务通知+TXE中断）*/
static uint8_t *tx_buf=NULL;
static uint16_t tx_len=0;
static uint16_t tx_idx=0;
static TaskHandle_t xTxTaskHandle=NULL;
static SemaphoreHandle_t xTxMutex=NULL;

/*Usart2接收数据环形缓冲区*/
#define ESP_BUF_LEN 512
uint8_t esp_rx_buf[ESP_BUF_LEN];
volatile uint16_t esp_rx_head=0;//写指针，中断往里写
volatile uint16_t esp_rx_tail=0;//读指针
SemaphoreHandle_t xEspAckSem;//二值信号量，中断和任务同步
char*esp_ack_str;//当前在等待哪个应答字符串
volatile uint8_t ack_match_idx=0;//逐字节匹配索引

/*命令字符串*/
char * at         = "AT\r\n";
char * at_cwmode  = "AT+CWMODE=1\r\n";    // 设置为STA模式
char * at_rst     = "AT+RST\r\n"; 
char * at_cwjap   = "AT+CWJAP=\"MaLiMaLi\",\"MLWL6666\"\r\n";
char * at_cipmux  = "AT+CIPMUX=0\r\n";    // 开启单连接
char * at_cipstart= "AT+CIPSTART=\"TCP\",\"192.168.1.9\",1883\r\n";
char * at_cipmode = "AT+CIPMODE=1\r\n";   // 开启透传模式
char * at_cipsend = "AT+CIPSEND\r\n";     // 进入透传发送状态
/*
	通过USART2和ESP01交互
	tx---PA2
	Rx---PA3
*/

void Esp01_Usart2Init(void)
{
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA,ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2,ENABLE);
    
    GPIO_InitTypeDef Struct;
    Struct.GPIO_Mode = GPIO_Mode_AF;
    Struct.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
    Struct.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOA,&Struct);
    
    GPIO_PinAFConfig(GPIOA,GPIO_PinSource2,GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA,GPIO_PinSource3,GPIO_AF_USART2);
    
    USART_InitTypeDef Struct2;
    Struct2.USART_BaudRate = 115200;  //波特率
    Struct2.USART_HardwareFlowControl = USART_HardwareFlowControl_None; //硬件流控  不使用硬件信号控制数据传输
    Struct2.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;   //同时支持读写
    Struct2.USART_Parity = USART_Parity_No; //校验位
    Struct2.USART_StopBits = USART_StopBits_1;  //一个停止位
    Struct2.USART_WordLength = USART_WordLength_8b; //有效数据长度
    USART_Init(USART2,&Struct2);
	
	USART_Cmd(USART2,ENABLE);
}

void USART2_IRQHandler(void)
{
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	/*发送中断*/
	if(USART_GetITStatus(USART2,USART_IT_TXE)!=RESET)
	{
		/*还有数据需要发送*/
		if(tx_idx<tx_len)
		{
			USART_SendData(USART2,tx_buf[tx_idx++]);
		}
		else
		{
			/*全部发完，关TXE中断，通知任务*/
			USART_ITConfig(USART2,USART_IT_TXE,DISABLE);
			if(xTxTaskHandle!=NULL)
			{
				vTaskNotifyGiveFromISR(xTxTaskHandle,&xHigherPriorityTaskWoken);
				portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
			}
		}
	}	
	
	
	/*接收中断*/
	if(USART_GetITStatus(USART2,USART_IT_RXNE)!=RESET)
	{
		uint8_t ch=USART_ReceiveData(USART2);
		/*写入环形缓冲区*/
		uint16_t next=(esp_rx_head+1)%ESP_BUF_LEN;
		if(next!=esp_rx_tail)//满足条件则：环形缓冲区还没满
		{
			esp_rx_buf[esp_rx_head]=ch;
			esp_rx_head=next;
		}
		/*满了就丢弃这个字节*/
		
		/*逐字节匹配ack（仅在等待应答时）*/
		if(esp_ack_str)
		{
			if(ch==esp_ack_str[ack_match_idx])
			{
				ack_match_idx++;
				if(esp_ack_str[ack_match_idx]=='\0')//匹配成功了，释放信号量
				{
					ack_match_idx=0;
					xSemaphoreGiveFromISR(xEspAckSem,&xHigherPriorityTaskWoken);
					portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
				}
			}
			else
			{
				ack_match_idx=(ch==esp_ack_str[0])?1:0;	
			}
		}
	}
	
}

uint16_t esp_rx_available(void)
{
	return (esp_rx_head-esp_rx_tail+ESP_BUF_LEN)%ESP_BUF_LEN;
}

uint16_t esp_rx_read(uint8_t*dst,uint16_t max_len)
{
	uint16_t len=0;
	while(esp_rx_tail!=esp_rx_head&&len<max_len)
	{
		dst[len++]=esp_rx_buf[esp_rx_tail];
		esp_rx_tail=(esp_rx_tail+1)%ESP_BUF_LEN;
	}
	return len;
}


/*非透传模式下通过ESP01发数据的完整流程*/
uint8_t ESP01_SendData(uint8_t *data, uint16_t len)
{
    char cmd[32];
    sprintf(cmd, "AT+CIPSEND=%d\r\n", len);//把长度拼进命令

    /* 发 CIPSEND 命令，等 ">" 提示符 */
    if (!ESP01_SendCmd(cmd, ">", 3000))
    {
        log_e("CIPSEND no >");
        return 0;
    }

    /* Arm acknowledgement matching before transmission; ESP may reply immediately. */
    esp_rx_tail = esp_rx_head;
    ack_match_idx = 0;
    esp_ack_str = "SEND OK";

    /* 发实际数据 */
    Usart2_SendString(data);

    if (xSemaphoreTake(xEspAckSem, pdMS_TO_TICKS(5000)) != pdTRUE)
    {
         uint8_t dbg[128];
		uint16_t n = esp_rx_read(dbg, sizeof(dbg)-1);
		dbg[n] = '\0';
		log_e("no SEND OK, got: %s", dbg);
		return 0;
    }
    esp_ack_str = NULL;
    return 1;
}

//串口2的字符串发送函数
void Usart2_SendString(uint8_t *str)
{
	uint8_t n;
	for(n=0;str[n]!='\0';n++)
	{
		USART_SendData(USART2,str[n]);
		while(USART_GetFlagStatus(USART2,USART_FLAG_TXE)==RESET);
	}
}

/*发送模块初始化*/
void Usart2_TxInit(void)
{
	if(xTxMutex==NULL)
	{
		xTxMutex=xSemaphoreCreateMutex();
	}
	tx_buf=NULL;
	tx_len=0;
	tx_idx=0;
	xTxTaskHandle=NULL;
}

/*设置哪个任务在等待发送完成（中断中通知它）*/
void Usart2_SetTxTask(TaskHandle_t handle)
{
	xTxTaskHandle=handle;
}

/*非阻塞二进制发送：发完才返回，期间任务阻塞不占CPU*/
uint8_t Usart2_SendBinary(uint8_t*data,uint16_t len,uint32_t timeout_ms)
{
	if((data==NULL)||(len==0)||(xTxMutex==NULL))
	{
		return 0;
	}
	/*拿互斥锁：防止多个任务同时发产生冲突*/
	if(xSemaphoreTake(xTxMutex,pdMS_TO_TICKS(timeout_ms))!=pdTRUE)
	{
		return 0;
	}
	/*设置发送缓冲区*/
	tx_buf=data;
	tx_len=len;
	tx_idx=0;
	/*清空之前的通知计数*/
	ulTaskNotifyTake(pdTRUE,0);
	/*发第一个字节，后续字节由TXE中断发*/
	USART_SendData(USART2,tx_buf[tx_idx++]);
	USART_ITConfig(USART2,USART_IT_TXE,ENABLE);
	/*阻塞等中断发完，超时返回失败*/
	if(!ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(timeout_ms)))
	{
		USART_ITConfig(USART2,USART_IT_TXE,DISABLE);
		xSemaphoreGive(xTxMutex);
		return 0;
	}
	xSemaphoreGive(xTxMutex);
	return 1;
}
/*
	esp01发送命令函数
*/
uint8_t ESP01_SendCmd(char*cmd,char*ack,uint16_t waittime)
{
	esp_rx_tail=esp_rx_head;/* 丢弃未读数据，等效清空 */
	ack_match_idx=0;
	esp_ack_str=ack;
	Usart2_SendString((uint8_t*)cmd);
	if(xSemaphoreTake(xEspAckSem,pdMS_TO_TICKS(waittime))==pdTRUE)
	{
		esp_ack_str=NULL;//清空等待目标
		return 1;
	}
	esp_ack_str=NULL;
	return 0;
}

/* 退出透传模式：发 "+++"，前后各1秒空闲，不加 \r\n */
static void Esp01_ExitPassthrough(void)
{
    /* 前导空闲：1秒内不发任何数据，让模块识别 +++ 时序 */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* 发 "+++"，注意：不能加 \r\n */
    Usart2_SendString((uint8_t *)"+++");

    /* 后导空闲：等模块退出透传并返回 OK */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* 清空接收缓冲区，把 "OK" 和残留数据清掉，避免影响后续 AT 匹配 */
    esp_rx_tail = esp_rx_head;
    ack_match_idx = 0;
}


/*单独创建一个TCP连接函数：防止出现TCP断连后无法重连的情况*/
uint8_t ESP01_ConnectTCP(const char*ip,uint16_t port,uint32_t timeout_ms)
{
	char cmd[64];
	
	/* ===== 第0步：重连准备 ===== */
    /* 退出透传模式（如果上次卡在透传里） */
    Esp01_ExitPassthrough();
	
	/* 新增：确认退出透传，发AT等OK */
	if(!ESP01_SendCmd("AT\r\n", "OK", 1000))
	{
		log_e("exit passthrough failed, module not in AT mode");
		return 0;
	}
	
	/* 关闭旧连接（失败也无所谓，可能本来就没连接） */
    ESP01_SendCmd("AT+CIPCLOSE\r\n", "OK", 2000);
    vTaskDelay(pdMS_TO_TICKS(500));
	
	/* 清空残留数据 */
    esp_rx_tail = esp_rx_head;
    ack_match_idx = 0;
	
	/*单连接模式*/
	if(!ESP01_SendCmd("AT+CIPMUX=0\r\n","OK",2000))
	{
		log_e("CIPMUX failed");
		return 0;
	}
	/*建立TCP连接*/
	snprintf(cmd,sizeof(cmd),"AT+CIPSTART=\"TCP\",\"%s\",%d\r\n",ip,port);
	if(!ESP01_SendCmd(cmd,"CONNECT",timeout_ms))
	{
		/* 新增：打印模块返回的原始数据，看看到底返回了什么 */
		uint8_t dbg[256];
		uint16_t n = esp_rx_read(dbg, sizeof(dbg)-1);
		dbg[n] = '\0';
		log_e("CIPSTART failed, module returned: %s", dbg);
		return 0;
	}
	log_i("TCP connected to %s:%d",ip,port);
	/*设置透传模式*/
	if(!ESP01_SendCmd("AT+CIPMODE=1\r\n","OK",2000))
	{
		log_e("CIPMODE failed");
		return 0;
	}
	 /* 4. 进入透传发送 */
    if (!ESP01_SendCmd("AT+CIPSEND\r\n", ">", 3000))
    {
        log_e("CIPSEND no >");
        return 0;
    }
    /* ===== 第5步（关键）：进入透传后清空残留 ===== */
    /* ">" 后面可能跟 \r\n，模块可能还有其他输出，
       等100ms让模块把残留数据都发完，然后清空缓冲区 */
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_rx_tail = esp_rx_head;
    ack_match_idx = 0;

    log_i("passthrough ready");
    return 1;
}

/*探测 TCP 连接状态：发 AT，能回 OK 说明模块在线且不在透传（已断开）
   在透传模式下发 AT 不会回 OK，所以能回 OK 反而是断开了 */
uint8_t ESP01_IsTCPConnected(void)
{
    /* 透传模式下无法用 AT 探测，这里简化处理：
       由 mqtt_app 维护连接状态，发送失败即认为断开 */
    return 1;
}

/*esp01初始化函数*/
void Esp01_Init(void)
{
	//一条条发送指令
	//不仅配置各种模式而且要连接服务器（裸机内实现发送接收）
	Esp01_Usart2Init();
	USART_ITConfig(USART2,USART_IT_RXNE,ENABLE);
	
	NVIC_InitTypeDef nvic;
    nvic.NVIC_IRQChannel = USART2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 6;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
	
	xEspAckSem = xSemaphoreCreateBinary();
	
	Esp01_ExitPassthrough();
	
	//vTaskDelay(pdMS_TO_TICKS(2000));//等待ESP01启动
	
	log_i("ESP01: AT test...");
	//vTaskDelay(pdMS_TO_TICKS(2000));
    if (!ESP01_SendCmd(at, "OK", 1000)) { log_e("AT test failed"); return; }

    log_i("ESP01: set STA mode...");
    if (!ESP01_SendCmd(at_cwmode, "OK", 2000)) { log_e("CWMODE failed"); return; }

    log_i("ESP01: connect WiFi...");
    if (!ESP01_SendCmd(at_cwjap, "OK", 15000)) { log_e("CWJAP failed"); return; }

    log_i("ESP01: single link...");
    if (!ESP01_SendCmd(at_cipmux, "OK", 1000)) { log_e("CIPMUX failed"); return; }
	ESP01_SendCmd("AT+CIFSR\r\n", "OK", 2000);

//    log_i("ESP01: connect server...");
//    if (!ESP01_SendCmd(at_cipstart, "CONNECT", 10000)) { log_e("CIPSTART failed"); return; }

//    log_i("ESP01: transparent mode...");
//    if (!ESP01_SendCmd(at_cipmode, "OK", 1000)) { log_e("CIPMODE failed"); return; }
// 
//    log_i("ESP01: enter passthrough...");
//    if (!ESP01_SendCmd(at_cipsend, ">", 3000)) { log_e("CIPSEND failed"); return; }

    log_i("ESP01: ready!");
}

/*服务器下发的消息通过串口中断接收*/
