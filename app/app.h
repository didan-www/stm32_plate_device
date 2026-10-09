#ifndef _APP_H__
#define _APP_H__
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdint.h>

/*==============================
 * 互斥锁
 *==============================*/
extern SemaphoreHandle_t xSensorMutex;   /* 保护 g_sensor 显示缓存 */
extern SemaphoreHandle_t xFlashMutex;    /* 保护 Flash / FatFs 访问 */

/*==============================
 * 任务句柄
 *==============================*/
extern TaskHandle_t StartTask_TaskHandle;
extern TaskHandle_t DHT11_TaskHandle;
extern TaskHandle_t SRF05_TaskHandle;
extern TaskHandle_t OLED_TaskHandle;
extern TaskHandle_t Event_Dispatch_TaskHandle;
extern TaskHandle_t ESP01_TaskHandle;
extern TaskHandle_t Camera_Task_Handle;
extern TaskHandle_t MQTT_KeepAliveTaskHandle;

/*===========================================================================
 * 双队列（核心架构）
 *   Uplink_Queue   : 采集任务 → 通信任务（设备→服务端）
 *   Downlink_Queue : 通信任务 → 分发任务（服务端→设备）
 *===========================================================================*/
extern QueueHandle_t Uplink_Queue;
extern QueueHandle_t Downlink_Queue;

#define UPLINK_QUEUE_LEN    32
#define DOWNLINK_QUEUE_LEN  16

/*显示缓存（仅OLED用，不再作为上传数据源）*/
typedef struct {
    float temp;
    float humi;
    float dist;
} sensor_data_t;
extern sensor_data_t g_sensor;

/*上行消息类型*/
typedef enum {
    UPLINK_SENSOR_DHT11  = 0,   /* 温湿度数据 */
    UPLINK_SENSOR_SR04   = 1,   /* 测距数据 */
    UPLINK_IMAGE_READY   = 2,   /* 图片已存Flash，待上传 */
    UPLINK_DEVICE_STATUS = 3,   /* 设备状态（online/offline） */
} UplinkType_t;

typedef struct {
    UplinkType_t type;
    union {
        struct {float temp;float humi;} dht11;
        float distance;
        struct { uint32_t file_id; char path[32]; } image;
        struct { char status[16]; } device;
    } data;
} UplinkMsg_t;

/*下行消息类型*/
typedef enum {
    DOWNLINK_CMD_OLED     = 0,   /* OLED 显示 */
    DOWNLINK_CMD_REBOOT   = 1,   /* 系统重启 */
    DOWNLINK_CMD_CAPTURE  = 2,   /* 触发拍照 */
    DOWNLINK_CMD_INTERVAL = 3,   /* 设置采样间隔（预留） */
} DownlinkType_t;

typedef struct {
    DownlinkType_t type;
    union {
        struct { uint8_t line; char text[32]; } oled;
        struct { uint8_t mode; } capture;       /* 0=上传 1=仅测试 */
        struct { uint16_t interval_ms; } interval;
    } data;
} DownlinkMsg_t;

extern QueueHandle_t Event_Queue;

typedef enum {
    EVT_SENSOR_DHT11,
    EVT_SENSOR_SR04,
    EVT_CMD_OLED,
    EVT_CMD_REBOOT,
    EVT_IMAGE_CAPTURED,
} EventType_t;

typedef struct {
    EventType_t type;
    union {
        struct {uint16_t temp;uint16_t humi;} dht11;
        float distance;
        struct {uint8_t line;char text[32];} oled_cmd;
        struct { uint32_t file_id; } image;
        uint32_t file_id;
    } data;
} Event_t;


/*任务函数声明*/
void Start_Task(void *arg);
void DHT11_FUN(void *arg);
void SRF05_FUN(void *arg);
void OLED_FUN(void *arg);
void Event_Dispatch_FUN(void *arg);
void ESP01_Test_FUN(void *arg);

#endif