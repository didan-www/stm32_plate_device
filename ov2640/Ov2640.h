#ifndef __OV2640_H__
#define __OV2640_H__

#include "stm32f4xx.h"
#include "Sccb.h"
#include <stdint.h>

/*===========================================================================
 * OV2640    ŷ   
 *
 *   SCCB                                DCMI     /ͬ  
 *     SCL -- PD6        PWDN -- PD11      D0 -- PC6    PCLK  -- PA6
 *     SDA -- PD7        RST  -- PD12      D1 -- PC7    HSYNC -- PA4
 *                                         D2 -- PE0    VSYNC -- PB7
 *                                         D3 -- PE1
 *                                         D4 -- PC11
 *                                         D5 -- PB6
 *                                         D6 -- PE5
 *                                         D7 -- PE6
 *===========================================================================*/
#define OV2640_PWDN_LOW()   GPIO_ResetBits(GPIOD, GPIO_Pin_11)
#define OV2640_PWDN_HIGH()  GPIO_SetBits(GPIOD,   GPIO_Pin_11)
#define OV2640_RST_LOW()    GPIO_ResetBits(GPIOD, GPIO_Pin_12)
#define OV2640_RST_HIGH()   GPIO_SetBits(GPIOD,   GPIO_Pin_12)

/* оƬ ID */
#define OV2640_MID   0x7FA2
#define OV2640_PID   0x2642

/* DSP  Ĵ    (Bank 0xFF=0x00) */
#define OV2640_DSP_R_BYPASS     0x05
#define OV2640_DSP_Qs           0x44
#define OV2640_DSP_CTRL         0x50
#define OV2640_DSP_HSIZE1       0x51
#define OV2640_DSP_VSIZE1       0x52
#define OV2640_DSP_XOFFL        0x53
#define OV2640_DSP_YOFFL        0x54
#define OV2640_DSP_VHYX         0x55
#define OV2640_DSP_TEST         0x57
#define OV2640_DSP_ZMOW         0x5A
#define OV2640_DSP_ZMOH         0x5B
#define OV2640_DSP_ZMHH         0x5C
#define OV2640_DSP_BPADDR       0x7C
#define OV2640_DSP_BPDATA       0x7D
#define OV2640_DSP_SIZEL        0x8C
#define OV2640_DSP_HSIZE2       0xC0
#define OV2640_DSP_VSIZE2       0xC1
#define OV2640_DSP_IMAGE_MODE   0xDA
#define OV2640_DSP_RESET        0xE0
#define OV2640_DSP_RA_DLMT      0xFF

/* Sensor  Ĵ    (Bank 0xFF=0x01) */
#define OV2640_SENSOR_PIDH      0x0A
#define OV2640_SENSOR_PIDL      0x0B
#define OV2640_SENSOR_COM7      0x12
#define OV2640_SENSOR_MIDH      0x1C
#define OV2640_SENSOR_MIDL      0x1D

/*===========================================================================
 * API
 *===========================================================================*/
uint8_t OV2640_Init(void);
void    OV2640_JPEG_Mode(void);
void    OV2640_RGB565_Mode(void);
void    OV2640_Auto_Exposure(uint8_t level);
void    OV2640_Light_Mode(uint8_t mode);
void    OV2640_Color_Saturation(uint8_t sat);
void    OV2640_Brightness(uint8_t bright);
void    OV2640_Contrast(uint8_t contrast);
void    OV2640_Special_Effects(uint8_t eft);
void    OV2640_Color_Bar(uint8_t sw);
void    OV2640_Window_Set(uint16_t sx, uint16_t sy,
                          uint16_t width, uint16_t height);
uint8_t OV2640_OutSize_Set(uint16_t width, uint16_t height);
uint8_t OV2640_ImageWin_Set(uint16_t offx, uint16_t offy,
                            uint16_t width, uint16_t height);
uint8_t OV2640_ImageSize_Set(uint16_t width, uint16_t height);

#endif /* __OV2640_H__ */
