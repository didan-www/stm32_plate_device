#include "stm32f4xx.h"
#include "app.h"
#include "device_config.h"
#include "myled.h"
#include "MyIIC.h"
#include "oled.h"
#include "stdio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "dht11.h"
#include "mytime.h"
#include "esp01.h"
#include "string.h"
#include "mqtt_app.h"
#include "Camer_task.h"
#include "http_client.h"
#include "w25q64.h"
#include "ff.h"

#define LOG_TAG "app.c"
#define LOG_LVL ELOG_LVL_VERBOSE
#include "elog.h"

extern void Camera_Task(void *arg);

/*全锟街憋拷锟斤拷锟斤拷锟斤拷*/
TaskHandle_t StartTask_TaskHandle      = NULL;
TaskHandle_t DHT11_TaskHandle          = NULL;
TaskHandle_t SRF05_TaskHandle          = NULL;
TaskHandle_t OLED_TaskHandle           = NULL;
TaskHandle_t Event_Dispatch_TaskHandle = NULL;
TaskHandle_t ESP01_TaskHandle   	   = NULL;
TaskHandle_t MQTT_KeepAliveTaskHandle = NULL;

SemaphoreHandle_t xSensorMutex = NULL;
SemaphoreHandle_t xFlashMutex  = NULL;

QueueHandle_t Uplink_Queue   = NULL;
QueueHandle_t Downlink_Queue = NULL;
QueueHandle_t Event_Queue    = NULL;

sensor_data_t     g_sensor     = {0};

extern volatile uint16_t esp_rx_head;//写指锟诫，锟叫讹拷锟斤拷锟斤拷写
extern volatile uint16_t esp_rx_tail;//锟斤拷指锟斤拷
extern char * at_cipstart;
/* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 < 50cm 锟斤拷锟斤拷锟斤拷锟秸后，撅拷锟斤拷指锟角帮拷锟斤拷馗锟斤拷锟斤拷锟?*/
static uint8_t photo_triggered = 0;
static TickType_t photo_next_tick = 0;

/* FatFs瀵硅薄蹇呴』鍦ㄦ暣涓繍琛屾湡闂翠繚鎸佹湁鏁堬紝涓嶈兘瀹氫箟鎴愬惎鍔ㄥ嚱鏁扮殑灞€閮ㄥ彉閲忋€?*/
static FATFS s_flash_fs;
static uint8_t s_mkfs_work[FF_MAX_SS];

static uint8_t flash_filesystem_init(void)
{
    FRESULT result;
    uint32_t flash_id;

    W25Q64_Init();
    flash_id = W25Q64_ReadID();
    if(flash_id != W25Q64_ID)
    {
        log_e("W25Q64 ID error: 0x%04lX, expect 0x%04X",
              (unsigned long)flash_id, W25Q64_ID);
        return 1;
    }

    result = f_mount(&s_flash_fs, "0:", 1);
    if(result == FR_NO_FILESYSTEM)
    {
        MKFS_PARM option;

        /* 璇lash涓撶敤浜庡浘鐗囧瓨鍌紝棣栨浣跨敤鏃跺垱寤篎AT鏂囦欢绯荤粺銆?*/
        memset(&option, 0, sizeof(option));
        option.fmt   = FM_FAT | FM_SFD;
        option.n_fat = 1;

        log_w("W25Q64 has no filesystem, formatting...");
        result = f_mkfs("0:", &option, s_mkfs_work, sizeof(s_mkfs_work));
        if(result != FR_OK)
        {
            log_e("FatFs format failed: %d", result);
            return 2;
        }

        (void)f_mount(NULL, "0:", 0);
        result = f_mount(&s_flash_fs, "0:", 1);
    }

    if(result != FR_OK)
    {
        log_e("FatFs mount failed: %d", result);
        return 3;
    }

    log_i("W25Q64 ready, ID=0x%04lX, FatFs mounted",
          (unsigned long)flash_id);
    return 0;
}


/*锟斤拷锟竭猴拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷息锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷印锟斤拷锟斤拷*/
static void uplink_send(const UplinkMsg_t *msg)
{
    if(Uplink_Queue == NULL) return;
    if(xQueueSend(Uplink_Queue, msg, 0) != pdTRUE)
    {
        /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷傻锟揭伙拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷碌锟?*/
        UplinkMsg_t old;
        xQueueReceive(Uplink_Queue, &old, 0);  /* 锟斤拷锟斤拷傻锟?*/
        xQueueSend(Uplink_Queue, msg, 0);       /* 锟斤拷锟斤拷锟铰碉拷 */
        log_w("uplink queue FULL, dropped oldest, type=%d", msg->type);
    }
}


static void downlink_send(const DownlinkMsg_t *msg)
{
    if(Downlink_Queue == NULL) return;
    if(xQueueSend(Downlink_Queue, msg, 0) != pdTRUE)
        log_w("downlink queue FULL, type=%d dropped", msg->type);
}

/*===========================================================================
 * DHT11 锟缴硷拷锟斤拷锟斤拷
 *   锟缴硷拷 锟斤拷 锟斤拷锟斤拷锟斤拷示锟斤拷锟斤拷 g_sensor 锟斤拷 锟斤拷锟斤拷锟叫讹拷锟斤拷
 *===========================================================================*/
void DHT11_FUN(void *arg)
{
    DHT11_Data_t d;
    uint8_t ret;
    uint8_t initialized = 0;

    while (1)
    {
        /* 缃戠粶鏈氨缁椂鏆傚仠浼犳劅鍣紝浼樺厛淇濊瘉ESP01瀹屾垚寤洪摼銆?*/
        if(!MQTT_IsConnected())
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if(!initialized)
        {
            DHT11_Init();
            initialized = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
        ret = DHT11_Read(&d);
        if (ret == 0)
        {
            float t = d.temp_int + d.temp_dec * 0.1f;
            float h = d.humidity_int + d.humidity_dec * 0.1f;

            /* 锟斤拷锟斤拷锟斤拷示锟斤拷锟芥（锟斤拷OLED锟矫ｏ拷 */
            xSemaphoreTake(xSensorMutex, portMAX_DELAY);
            g_sensor.temp = t;
            g_sensor.humi = h;
            xSemaphoreGive(xSensorMutex);

            /* 锟斤拷锟斤拷锟叫讹拷锟叫ｏ拷锟斤拷ESP01锟较达拷锟矫ｏ拷 */
            UplinkMsg_t msg = {0};
            msg.type = UPLINK_SENSOR_DHT11;
            msg.data.dht11.temp = t;
            msg.data.dht11.humi = h;
            uplink_send(&msg);
        }
        else
        {
            log_e("dht11 read fail: %d", ret);
        }
    }
}

/*===========================================================================
 * SRF05 锟斤拷锟斤拷锟斤拷锟?
 *   锟缴硷拷 锟斤拷 锟斤拷锟斤拷锟斤拷示锟斤拷锟斤拷 锟斤拷 锟斤拷锟斤拷锟叫讹拷锟斤拷 锟斤拷 <50cm 锟斤拷锟斤拷锟斤拷锟斤拷
 *===========================================================================*/
void SRF05_FUN(void *arg)
{
    float dist;
    uint8_t initialized = 0;

    while(1)
    {
        /* 缃戠粶鏈氨缁椂涓嶈Е鍙戞祴璺濆拰鎷嶇収銆?*/
        if(!MQTT_IsConnected())
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if(!initialized)
        {
            Srf05_Init();
            initialized = 1;
        }
        dist = Srf05_ReadDistance();

        if(dist >= 0)
        {
            /* 锟斤拷锟斤拷锟斤拷示锟斤拷锟斤拷 */
            xSemaphoreTake(xSensorMutex, portMAX_DELAY);
            g_sensor.dist = dist;
            xSemaphoreGive(xSensorMutex);

            /* 锟斤拷锟斤拷锟叫讹拷锟斤拷 */
            UplinkMsg_t msg = {0};
            msg.type = UPLINK_SENSOR_SR04;
            msg.data.distance = dist;
            uplink_send(&msg);

            /* 锟斤拷锟斤拷 < 50cm 锟斤拷锟斤拷锟斤拷锟秸ｏ拷锟借备锟节诧拷锟竭硷拷锟斤拷锟斤拷锟斤拷锟斤拷锟叫讹拷锟叫ｏ拷 */
            if(dist < 50.0f && dist > 0.0f)
            {
                TickType_t now = xTaskGetTickCount();
                if(!photo_triggered || (int32_t)(now - photo_next_tick) >= 0)
                {
                    photo_triggered = 1;
                    photo_next_tick = now + pdMS_TO_TICKS(3000);
                    Camera_TriggerCapture(CAM_MODE_UPLOAD);
                    log_i("photo trigger -> capture+upload, dist=%.1fcm", dist);
                }
            }
            else
            {
                photo_triggered = 0;
            }
        }
        else
        {
            log_w("sr04 no echo");
            /* 鏈娴嬮噺瓒呮椂涔熼€氱煡閫氫俊浠诲姟锛岄伩鍏嶇户缁彂甯冧笂涓€娆＄殑鏃ц窛绂汇€?*/
            UplinkMsg_t invalid = {0};
            invalid.type = UPLINK_SENSOR_SR04;
            invalid.data.distance = -1.0f;
            uplink_send(&invalid);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


/*===========================================================================
 * OLED 锟斤拷示锟斤拷锟斤拷
 *   只锟斤拷 g_sensor 锟斤拷示锟斤拷锟芥，锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷/锟斤拷锟斤拷
 *===========================================================================*/
void OLED_FUN(void *arg)
{
    sensor_data_t s;

    My_IIC_GPIOInit();
    OLED_Init();
    OLED_Clear();

    while(1)
    {
        xSemaphoreTake(xSensorMutex, portMAX_DELAY);
        s = g_sensor;
        xSemaphoreGive(xSensorMutex);

        Oled_DisplayAline();  /* TODO: 锟芥换为实锟斤拷锟斤拷示锟斤拷锟斤拷 */

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/*===========================================================================
 * 锟斤拷锟叫斤拷锟斤拷锟斤拷锟斤拷 esp_rx_buf 锟斤拷取 +IPD 锟斤拷锟捷ｏ拷锟斤拷锟斤拷 JSON 锟斤拷 Downlink_Queue
 *   目前锟斤拷锟街凤拷锟斤拷匹锟斤拷锟斤拷锟斤拷示锟斤拷锟斤拷式锟斤拷目锟斤拷锟斤拷锟斤拷 cJSON 锟斤拷锟斤拷
 *===========================================================================*/
static void parse_downlink_to_queue(void)
{
    static uint8_t rx_buf[512];
    uint16_t n = esp_rx_read(rx_buf, sizeof(rx_buf) - 1);
    if(n == 0) return;
    rx_buf[n] = '\0';

    /* 锟斤拷锟斤拷 +IPD,<len>:  */
    char *ipd = strstr((char*)rx_buf, "+IPD,");
    if(ipd == NULL) return;

    char *colon = strchr(ipd, ':');
    if(colon == NULL) return;

    char *json = colon + 1;

    /* 锟斤拷锟斤拷 "cmd" 锟街段分凤拷 */
    if(strstr(json, "\"reboot\"") != NULL)
    {
        DownlinkMsg_t msg = {0};
        msg.type = DOWNLINK_CMD_REBOOT;
        downlink_send(&msg);
        log_i("downlink: REBOOT");
    }
    else if(strstr(json, "\"capture\"") != NULL)
    {
        DownlinkMsg_t msg = {0};
        msg.type = DOWNLINK_CMD_CAPTURE;
        msg.data.capture.mode = 0;
        downlink_send(&msg);
        log_i("downlink: CAPTURE");
    }
    else if(strstr(json, "\"oled\"") != NULL)
    {
        DownlinkMsg_t msg = {0};
        msg.type = DOWNLINK_CMD_OLED;
        msg.data.oled.line = 0;
        char *text_p = strstr(json, "\"text\":\"");
        if(text_p != NULL)
        {
            text_p += 8;
            char *end = strchr(text_p, '"');
            if(end != NULL)
            {
                uint16_t len = (uint16_t)(end - text_p);
                if(len >= sizeof(msg.data.oled.text))
                    len = sizeof(msg.data.oled.text) - 1;
                memcpy(msg.data.oled.text, text_p, len);
                msg.data.oled.text[len] = '\0';
            }
        }
        downlink_send(&msg);
        log_i("downlink: OLED text=%s", msg.data.oled.text);
    }
    else
    {
        log_w("downlink: unknown cmd: %.120s", json);
    }
}

/*===========================================================================
 * ESP01 通锟斤拷锟斤拷锟斤拷
 *   锟斤拷 Uplink_Queue 取锟斤拷锟斤拷 锟斤拷 锟斤拷 MQTT / HTTP
 *   锟斤拷 esp_rx_buf 锟斤拷锟斤拷锟斤拷 锟斤拷 锟斤拷锟斤拷 锟斤拷 锟斤拷 Downlink_Queue
 *   锟斤拷锟斤拷直锟接讹拷 g_sensor
 *===========================================================================*/
void ESP01_Test_FUN(void *arg)
{
    UplinkMsg_t msg;
    float latest_temp = 0.0f, latest_humi = 0.0f, latest_dist = -1.0f;
    uint8_t has_dht11 = 0, has_sr04 = 0;
    TickType_t last_publish = 0;

    log_i("ESP01 comm task start");

    /* 1. 锟斤拷始锟斤拷 ESP01 + MQTT */
    Esp01_Init();
    MQTT_Init();

    /* 2. 锟斤拷循锟斤拷 */
    while(1)
    {
        /* ---- 2.1 锟斤拷锟饺达拷锟斤拷锟斤拷锟斤拷锟斤拷锟捷ｏ拷锟斤拷锟解环锟轿伙拷锟斤拷锟斤拷锟斤拷锟?---- */
        if(esp_rx_available() > 0)
        {
            MQTT_ProcessRX();
        }

        /* ---- 2.2 MQTT 未锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 ---- */
        if(!MQTT_IsConnected())
        {
            uint32_t delay = MQTT_GetReconnectDelay();
            log_i("connecting MQTT in %dms...", (int)delay);
            vTaskDelay(pdMS_TO_TICKS(delay));

            if(MQTT_Connect(DEVICE_MQTT_CLIENT_ID, 60) == MQTT_OK &&
               MQTT_Subscribe(DEVICE_RESULT_TOPIC, 1) == MQTT_OK)
            {
                log_i("MQTT ready and result topic subscribed");
				/* 锟斤拷 ESP01_Test_FUN 锟斤，MQTT_Connect 锟缴癸拷之锟斤拷锟斤拷锟斤拷 */
				if (MQTT_IsConnected())
				{
					if (Camera_Task_Handle == NULL)
					{
                        xTaskCreate(Camera_Task, "Camera", 4096, NULL, 4, &Camera_Task_Handle);
						log_i("Camera task created after MQTT ready");
					}
				}

            }
            else
            {
                log_e("MQTT connect failed");
				/* 锟斤拷锟斤拷锟斤拷MQTT没锟斤拷锟较ｏ拷锟窖讹拷锟斤拷锟斤拷锟窖癸拷锟斤拷锟斤拷锟斤拷锟秸ｏ拷锟斤拷止锟斤拷锟斤拷锟斤拷 */
				UplinkMsg_t drop;
				while(xQueueReceive(Uplink_Queue, &drop, 0) == pdTRUE);
				continue;
            }
        }

        /* ---- 2.3 锟斤拷锟斤拷锟叫讹拷锟斤拷取锟斤拷息锟斤拷锟斤拷100ms锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷询锟斤拷 ---- */
        if(xQueueReceive(Uplink_Queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            switch(msg.type)
            {
                case UPLINK_SENSOR_DHT11:
                    latest_temp = msg.data.dht11.temp;
                    latest_humi = msg.data.dht11.humi;
                    has_dht11 = 1;
                    break;

                case UPLINK_SENSOR_SR04:
                    latest_dist = msg.data.distance;
                    has_sr04 = 1;
                    break;

                case UPLINK_IMAGE_READY:
                    /* 图片锟斤拷 HTTP 锟较达拷锟斤拷锟斤拷锟斤拷 MQTT */
                    log_i("image ready: %s, start HTTP upload",
                          msg.data.image.path);
                    {
                        uint8_t http_result;

                        /* ESP01涓哄崟杩炴帴妯″紡锛孒TTP涓婁紶鏈熼棿鏆傚仠MQTT淇濇椿銆?*/
                        MQTT_ResetState();
                        http_result = HTTP_UploadFile(msg.data.image.path,
                                                      msg.data.image.file_id);
                        if(http_result == HTTP_OK)
                            log_i("HTTP image upload OK: %s", msg.data.image.path);
                        else
                            log_e("HTTP image upload failed: %u, file=%s",
                                  http_result, msg.data.image.path);
                    }
                    break;

                case UPLINK_DEVICE_STATUS:
                    /* Device account is read-only on MQTT. */
                    break;

                default:
                    log_w("unknown uplink type: %d", msg.type);
                    break;
            }
        }

        /* ---- 2.4 每 2 锟斤拷喜锟斤拷锟斤拷锟揭伙拷未锟斤拷锟斤拷锟斤拷锟斤拷锟?---- */
        if((xTaskGetTickCount() - last_publish) >= pdMS_TO_TICKS(2000))
        {
            if(has_dht11 || has_sr04)
            {
                char json[128];
                int json_len = snprintf(json, sizeof(json),
                    "{\"temp\":%.1f,\"humi\":%.1f,\"dist\":%.1f}",
                    latest_temp, latest_humi, latest_dist);

                if(json_len > 0 && json_len < (int)sizeof(json))
                {
                    log_d("sensor local only: %s", json);
                }
                else
                {
                    log_e("json overflow, len=%d", json_len);
                }
                last_publish = xTaskGetTickCount();
            }
        }
        /* xQueueReceive 锟窖撅拷锟斤拷锟斤拷100ms锟斤拷锟斤拷锟斤拷要锟斤拷 delay */
    }
}

/*===========================================================================
 * Event_Dispatch 锟斤拷锟斤拷锟斤拷锟叫分凤拷锟斤拷锟斤拷
 *   锟斤拷 Downlink_Queue 取锟斤拷锟斤拷 锟斤拷 锟街凤拷锟斤拷 OLED / 锟斤拷锟斤拷 / 锟斤拷锟斤拷
 *   锟斤拷锟劫达拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟捷ｏ拷锟斤拷锟斤拷锟斤拷锟叫碉拷锟铰ｏ拷
 *   锟斤拷锟劫碉拷 HTTP_UploadFile锟斤拷锟斤拷锟斤拷通锟斤拷锟斤拷锟斤拷锟斤拷拢锟?
 *===========================================================================*/
void Event_Dispatch_FUN(void *arg)
{
    DownlinkMsg_t cmd;
    Event_t evt;

    log_i("event dispatch task start");

    while(1)
    {
        /* 鍥剧墖淇濆瓨浜嬩欢杞崲涓轰笂琛屾秷鎭紝缃戠粶鎿嶄綔浠嶇敱ESP01浠诲姟鎵ц銆?*/
        if(xQueueReceive(Event_Queue, &evt, pdMS_TO_TICKS(20)) == pdTRUE)
        {
            if(evt.type == EVT_IMAGE_CAPTURED)
            {
                UplinkMsg_t msg = {0};
                msg.type = UPLINK_IMAGE_READY;
                msg.data.image.file_id = evt.data.file_id;
                snprintf(msg.data.image.path, sizeof(msg.data.image.path),
                         "0:IMG_%03lu.JPG",
                         (unsigned long)evt.data.file_id);
                uplink_send(&msg);
                log_i("image event queued: %s", msg.data.image.path);
            }
            else
            {
                log_w("dispatch: unknown event type %d", evt.type);
            }
        }

        if(xQueueReceive(Downlink_Queue, &cmd, pdMS_TO_TICKS(20)) != pdTRUE)
            continue;

        switch(cmd.type)
        {
            case DOWNLINK_CMD_OLED:
                log_i("dispatch: OLED line%d: %s",
                      cmd.data.oled.line, cmd.data.oled.text);
                /* TODO: 锟斤拷锟斤拷 OLED 锟斤拷示锟斤拷锟斤拷 */
                break;

            case DOWNLINK_CMD_REBOOT:
                log_w("dispatch: rebooting...");
                vTaskDelay(pdMS_TO_TICKS(500));
                NVIC_SystemReset();
                break;

            case DOWNLINK_CMD_CAPTURE:
                log_i("dispatch: trigger capture, mode=%d",
                      cmd.data.capture.mode);
                Camera_TriggerCapture(cmd.data.capture.mode);
                break;

            case DOWNLINK_CMD_INTERVAL:
                log_i("dispatch: set interval %dms",
                      cmd.data.interval.interval_ms);
                /* TODO: 锟斤拷态锟斤拷锟斤拷锟缴硷拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */
                break;

            default:
                log_w("dispatch: unknown cmd type %d", cmd.type);
                break;
        }
    }
}


/*===========================================================================
 * Start_Task锟斤拷锟斤拷锟斤拷双锟斤拷锟叫★拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷
 *===========================================================================*/
void Start_Task(void *arg)
{
    BaseType_t xReturn;

    Uplink_Queue = xQueueCreate(UPLINK_QUEUE_LEN, sizeof(UplinkMsg_t));
    log_i("Uplink_Queue %s", Uplink_Queue ? "OK" : "FAIL");

    Downlink_Queue = xQueueCreate(DOWNLINK_QUEUE_LEN, sizeof(DownlinkMsg_t));
    log_i("Downlink_Queue %s", Downlink_Queue ? "OK" : "FAIL");

    Event_Queue = xQueueCreate(16, sizeof(Event_t));
    log_i("Event_Queue %s", Event_Queue ? "OK" : "FAIL");

    xSensorMutex = xSemaphoreCreateMutex();
    log_i("SensorMutex %s", xSensorMutex ? "OK" : "FAIL");

    xFlashMutex = xSemaphoreCreateMutex();
    log_i("FlashMutex %s", xFlashMutex ? "OK" : "FAIL");

    if (Uplink_Queue == NULL || Downlink_Queue == NULL ||
        Event_Queue == NULL || xSensorMutex == NULL || xFlashMutex == NULL)
    {
        log_e("kernel object allocation failed; workers not started");
        vTaskDelete(NULL);
        return;
    }

    if(flash_filesystem_init() != 0)
        log_e("image storage unavailable");

    xReturn = xTaskCreate(ESP01_Test_FUN, "ESP01", 1024, NULL, 7,
                          &ESP01_TaskHandle);
    log_i("ESP01 task %s", xReturn == pdPASS ? "OK" : "FAIL");

    xReturn = xTaskCreate(MQTT_KeepAliveTask, "MQTT_Keep", 512, NULL, 2, &MQTT_KeepAliveTaskHandle);
    log_i("MQT_Keep task %s", xReturn == pdPASS ? "OK" : "FAIL");

    xReturn = xTaskCreate(DHT11_FUN, "DHT11", 512, NULL, 2,
                          &DHT11_TaskHandle);
    log_i("DHT11 task %s", xReturn == pdPASS ? "OK" : "FAIL");

    /* 鎽勫儚澶村湪MQTT寤洪摼鎴愬姛鍚庣敱ESP01浠诲姟鍒涘缓锛岄伩鍏嶅惎鍔ㄩ樁娈垫姠鍗犱覆鍙ｃ€?*/
    Camera_Task_Handle = NULL;
    log_i("Camera task waits for MQTT ready");

    xReturn = xTaskCreate(SRF05_FUN, "SRF05", 512, NULL, 2,
                          &SRF05_TaskHandle);
    log_i("SRF05 task %s", xReturn == pdPASS ? "OK" : "FAIL");

//    xReturn = xTaskCreate(OLED_FUN, "OLED", 1024, NULL, 2,
//                          &OLED_TaskHandle);
//    log_i("OLED task %s", xReturn == pdPASS ? "OK" : "FAIL");

    xReturn = xTaskCreate(Event_Dispatch_FUN, "Dispatch", 4096, NULL, 4,
                          &Event_Dispatch_TaskHandle);
    log_i("Dispatch task %s", xReturn == pdPASS ? "OK" : "FAIL");

    vTaskDelete(NULL);
}
