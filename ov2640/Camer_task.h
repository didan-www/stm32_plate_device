#ifndef __CAMERA_TASK_H__
#define __CAMERA_TASK_H__

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

/* ץ  ģʽ */
#define CAM_MODE_UPLOAD     0   /*    Flash        ϴ  */
#define CAM_MODE_TEST       1   /* ֻ ɼ      ڵ    */

extern TaskHandle_t Camera_Task_Handle;

void      Camera_Task(void *arg);
void      Camera_TriggerCapture(uint8_t mode);
uint8_t  *Camera_GetFrameBuf(void);
uint32_t  Camera_GetFrameSize(void);

/*    DCMI ֡ жϵ    */
void      jpeg_data_process(void);

#endif /* __CAMERA_TASK_H__ */