#ifndef __HTTP_CLIENT_H__
#define __HTTP_CLIENT_H__

#include "stm32f4xx.h"
#include <stdint.h>

/* HTTP服务器配置 */
#define HTTP_SERVER_IP      "192.168.1.9"
#define HTTP_SERVER_PORT    8089
#define HTTP_UPLOAD_PATH    "/api/v1/plate/upload"
#define HTTP_DEV_ID         "MaLi-001"

/* 单次发送的数据长度，不能超过ESP01缓冲区能力 */
#define HTTP_CHUNK_SIZE     1024U
#define HTTP_CHUNK_RETRY    3U

/* HTTP上传结果 */
#define HTTP_OK             0U
#define HTTP_ERR_CONNECT    1U
#define HTTP_ERR_SEND       2U
#define HTTP_ERR_RESP       3U
#define HTTP_ERR_FILE       4U
#define HTTP_ERR_ABORT      5U

/* 断点续传状态，保存在图片对应的ST文件中 */
typedef struct
{
    uint32_t total_size;
    uint32_t sent_offset;
    uint16_t retry_count;
    uint16_t magic;
} HttpUploadState_t;

#define HTTP_STATE_MAGIC    0xA55AU

uint8_t HTTP_UploadFile(const char *path, uint32_t img_id);
void    HTTP_RetryPending(void);
void    HTTP_ClearState(uint32_t img_id);

#endif /* __HTTP_CLIENT_H__ */
