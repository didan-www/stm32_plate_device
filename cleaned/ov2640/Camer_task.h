/**
 ******************************************************************************
 * @file    Camer_task.h
 * @brief   OV2640 摄像头采集任务头文件
 *
 *  功能:
 *    - 初始化 OV2640 (JPEG 模式, 640x480)
 *    - 通过 DCMI+DMA 采集 JPEG 帧
 *    - 查找 JPEG SOI(FFD8)/EOI(FFD9), 保存到 W25Q64 Flash
 *    - 采集完成后发送事件到 Event_Queue
 *
 *  采集模式:
 *    - CAM_MODE_UPLOAD  采集+保存Flash+发事件 (正常模式)
 *    - CAM_MODE_TEST    仅采集, 不保存 (测试模式)
 ******************************************************************************
 */
#ifndef __CAMERA_TASK_H__
#define __CAMERA_TASK_H__

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

/* 采集模式 */
#define CAM_MODE_UPLOAD     0   /* 采集+保存Flash+发事件 */
#define CAM_MODE_TEST       1   /* 仅采集, 不保存 (测试) */

/* 摄像头任务句柄 */
extern TaskHandle_t Camera_Task_Handle;

/**
 * @brief  摄像头任务入口
 * @param  arg  任务参数 (未使用)
 */
void Camera_Task(void *arg);

/**
 * @brief  触发一次拍照
 * @param  mode  采集模式 (CAM_MODE_UPLOAD / CAM_MODE_TEST)
 * @note   内部通过 xCaptureQueue 发送请求, 队列满时打印警告
 */
void Camera_TriggerCapture(uint8_t mode);

/**
 * @brief  获取当前帧缓冲区指针
 * @return JPEG 数据缓冲区首地址 (字节地址)
 */
uint8_t *Camera_GetFrameBuf(void);

/**
 * @brief  获取当前帧大小
 * @return 帧数据字节数
 */
uint32_t Camera_GetFrameSize(void);

/**
 * @brief  DCMI 帧中断回调 (在 DCMI_IRQHandler 中调用)
 * @note   处理帧尾剩余数据, 通知任务帧采集完成
 */
void jpeg_data_process(void);

#endif /* __CAMERA_TASK_H__ */
