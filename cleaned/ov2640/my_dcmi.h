/**
 ******************************************************************************
 * @file    my_dcmi.h
 * @brief   DCMI + DMA2_Stream1 驱动头文件
 *
 *  硬件连接:
 *    D0  -- PC6     D4  -- PC11    PCLK  -- PA6
 *    D1  -- PC7     D5  -- PB6     HSYNC -- PA4
 *    D2  -- PE0     D6  -- PE5     VSYNC -- PB7
 *    D3  -- PE1     D7  -- PE6
 *
 *  DMA: DMA2 Stream1 Channel1 (DCMI 只能用 Stream1 或 Stream7, Channel1)
 *
 *  注意: DMA 缓冲区必须放在普通 SRAM (0x2000xxxx), 不能放在 CCM (0x1000xxxx),
 *        因为 DMA2 无法访问 CCM.
 ******************************************************************************
 */
#ifndef __MY_DCMI_H__
#define __MY_DCMI_H__

#include "stm32f4xx.h"
#include <stdint.h>

/* 采集状态标志: 1=正在采集, 0=停止 */
extern volatile uint8_t g_dcmi_capturing;

/* DMA 传输完成(TC)中断回调函数指针, 由上层注册 */
extern void (*dcmi_rx_callback)(void);

/**
 * @brief  初始化 DCMI 外设及 GPIO
 * @note   配置为连续采集模式, VSYNC低有效, HSYNC低有效, PCLK上升沿采样
 */
void My_DCMI_Init(void);

/**
 * @brief  初始化 DMA2_Stream1
 * @param  mem0addr  缓冲区0地址 (必须在普通SRAM)
 * @param  mem1addr  缓冲区1地址, 非0时启用双缓冲模式
 * @param  memsize   缓冲区大小 (单位: 数据宽度, 此处为word)
 * @param  memblen   内存数据宽度 (DMA_MemoryDataSize_Word 等)
 * @param  meminc    内存增量模式 (DMA_MemoryInc_Enable / Disable)
 */
void DCMI_DMA_Init(uint32_t mem0addr, uint32_t mem1addr,
                   uint16_t memsize,
                   uint32_t memblen, uint32_t meminc);

/**
 * @brief  启动 DCMI 采集 (先清中断标志, 再开DMA, 再开采集)
 */
void DCMI_Start(void);

/**
 * @brief  停止 DCMI 采集 (关采集, 关DMA)
 */
void DCMI_Stop(void);

#endif /* __MY_DCMI_H__ */
