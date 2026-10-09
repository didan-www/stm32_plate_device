#define LOG_TAG "ov2640"

#include "Ov2640.h"
#include "Ov2640cfg.h"
#include "Sccb.h"
#include "mydelay.h"
#include "elog.h"

/*===========================================================================
 * OV2640_Init
 *      0= ɹ     0=ʧ  
 *===========================================================================*/
uint8_t OV2640_Init(void)
{
    uint16_t i;
    uint16_t reg;
    GPIO_InitTypeDef gpio;

    /* PWDN=PD11  RST=PD12 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_11 | GPIO_Pin_12;
    gpio.GPIO_Mode  = GPIO_Mode_OUT;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOD, &gpio);

    OV2640_PWDN_LOW();      /*  ϵ  */
    MyDelay_ms(10);
    OV2640_RST_LOW();       /*   λ */
    MyDelay_ms(10);
    OV2640_RST_HIGH();      /*  ͷŸ λ */
    MyDelay_ms(10);

    SCCB_Init();

    /*     λ */
    SCCB_WR_Reg(OV2640_DSP_RA_DLMT, 0x01);      /*  е  Sensor  Ĵ      */
    SCCB_WR_Reg(OV2640_SENSOR_COM7, 0x80);
    MyDelay_ms(50);

    /*    MID */
    reg  = SCCB_RD_Reg(OV2640_SENSOR_MIDH);
    reg <<= 8;
    reg |= SCCB_RD_Reg(OV2640_SENSOR_MIDL);
    if(reg != OV2640_MID)
    {
        log_e("MID mismatch: 0x%04X (expect 0x%04X)", reg, OV2640_MID);
        return 1;
    }

    /*    PID */
    reg  = SCCB_RD_Reg(OV2640_SENSOR_PIDH);
    reg <<= 8;
    reg |= SCCB_RD_Reg(OV2640_SENSOR_PIDL);
    if(reg != OV2640_PID)
    {
        log_e("PID mismatch: 0x%04X (expect 0x%04X)", reg, OV2640_PID);
        return 2;
    }

    log_i("OV2640 ID=0x%04X OK", reg);

    /* д SXGA       ʼ            һ       þ         */
    for(i = 0; i < sizeof(ov2640_sxga_init_reg_tbl) / 2; i++)
        SCCB_WR_Reg(ov2640_sxga_init_reg_tbl[i][0],
                    ov2640_sxga_init_reg_tbl[i][1]);

    log_i("OV2640 init OK");
    return 0;
}

/*===========================================================================
 * OV2640_JPEG_Mode
 *      Ƚ  YUV422      JPEG  ֱ  д JPEG       Ч
 *===========================================================================*/
void OV2640_JPEG_Mode(void)
{
    uint16_t i;

    for(i = 0; i < sizeof(ov2640_yuv422_reg_tbl) / 2; i++)
        SCCB_WR_Reg(ov2640_yuv422_reg_tbl[i][0],
                    ov2640_yuv422_reg_tbl[i][1]);

    for(i = 0; i < sizeof(ov2640_jpeg_reg_tbl) / 2; i++)
        SCCB_WR_Reg(ov2640_jpeg_reg_tbl[i][0],
                    ov2640_jpeg_reg_tbl[i][1]);
}

void OV2640_RGB565_Mode(void)
{
    uint16_t i;

    for(i = 0; i < sizeof(ov2640_rgb565_reg_tbl) / 2; i++)
        SCCB_WR_Reg(ov2640_rgb565_reg_tbl[i][0],
                    ov2640_rgb565_reg_tbl[i][1]);
}

/*===========================================================================
 *  ع  0~4
 *===========================================================================*/
static const uint8_t OV2640_AUTOEXPOSURE_LEVEL[5][8] =
{
    { 0xFF,0x01, 0x24,0x20, 0x25,0x18, 0x26,0x60 },
    { 0xFF,0x01, 0x24,0x34, 0x25,0x1C, 0x26,0x00 },
    { 0xFF,0x01, 0x24,0x3E, 0x25,0x38, 0x26,0x81 },
    { 0xFF,0x01, 0x24,0x48, 0x25,0x40, 0x26,0x81 },
    { 0xFF,0x01, 0x24,0x58, 0x25,0x50, 0x26,0x92 },
};

void OV2640_Auto_Exposure(uint8_t level)
{
    uint8_t i;
    const uint8_t *p;

    if(level > 4) level = 4;
    p = OV2640_AUTOEXPOSURE_LEVEL[level];

    for(i = 0; i < 4; i++)
        SCCB_WR_Reg(p[i*2], p[i*2+1]);
}

/*===========================================================================
 *   ƽ   0= Զ  1=     2=     3= 칫   4=    
 *===========================================================================*/
void OV2640_Light_Mode(uint8_t mode)
{
    uint8_t regccval = 0x5E;
    uint8_t regcdval = 0x41;
    uint8_t regceval = 0x54;

    switch(mode)
    {
        case 0:
            SCCB_WR_Reg(0xFF, 0x00);
            SCCB_WR_Reg(0xC7, 0x10);        /* AWB ON */
            return;
        case 2: regccval = 0x65; regcdval = 0x41; regceval = 0x4F; break;
        case 3: regccval = 0x52; regcdval = 0x41; regceval = 0x66; break;
        case 4: regccval = 0x42; regcdval = 0x3F; regceval = 0x71; break;
        default: break;
    }

    SCCB_WR_Reg(0xFF, 0x00);
    SCCB_WR_Reg(0xC7, 0x40);                /* AWB OFF */
    SCCB_WR_Reg(0xCC, regccval);
    SCCB_WR_Reg(0xCD, regcdval);
    SCCB_WR_Reg(0xCE, regceval);
}

/*===========================================================================
 *    Ͷ  0~4
 *===========================================================================*/
void OV2640_Color_Saturation(uint8_t sat)
{
    uint8_t reg7dval;

    if(sat > 4) sat = 4;
    reg7dval = ((sat + 2) << 4) | 0x08;

    SCCB_WR_Reg(0xFF, 0x00);
    SCCB_WR_Reg(0x7C, 0x00);
    SCCB_WR_Reg(0x7D, 0x02);
    SCCB_WR_Reg(0x7C, 0x03);
    SCCB_WR_Reg(0x7D, reg7dval);
    SCCB_WR_Reg(0x7D, reg7dval);
}

/*===========================================================================
 *      0~4
 *===========================================================================*/
void OV2640_Brightness(uint8_t bright)
{
    SCCB_WR_Reg(0xFF, 0x00);
    SCCB_WR_Reg(0x7C, 0x00);
    SCCB_WR_Reg(0x7D, 0x04);
    SCCB_WR_Reg(0x7C, 0x09);
    SCCB_WR_Reg(0x7D, (uint8_t)(bright << 4));
    SCCB_WR_Reg(0x7D, 0x00);
}

/*===========================================================================
 *  Աȶ  0~4
 *===========================================================================*/
void OV2640_Contrast(uint8_t contrast)
{
    uint8_t reg7d0val = 0x20;
    uint8_t reg7d1val = 0x20;

    switch(contrast)
    {
        case 0: reg7d0val = 0x18; reg7d1val = 0x34; break;
        case 1: reg7d0val = 0x1C; reg7d1val = 0x2A; break;
        case 3: reg7d0val = 0x24; reg7d1val = 0x16; break;
        case 4: reg7d0val = 0x28; reg7d1val = 0x0C; break;
        default: break;
    }

    SCCB_WR_Reg(0xFF, 0x00);
    SCCB_WR_Reg(0x7C, 0x00);
    SCCB_WR_Reg(0x7D, 0x04);
    SCCB_WR_Reg(0x7C, 0x07);
    SCCB_WR_Reg(0x7D, 0x20);
    SCCB_WR_Reg(0x7D, reg7d0val);
    SCCB_WR_Reg(0x7D, reg7d1val);
    SCCB_WR_Reg(0x7D, 0x06);
}

/*===========================================================================
 *   Ч 0=     1=  Ƭ 2= ڰ  3=ƫ   4=ƫ   5=ƫ   6=    
 *===========================================================================*/
void OV2640_Special_Effects(uint8_t eft)
{
    uint8_t reg7d0val = 0x00;
    uint8_t reg7d1val = 0x80;
    uint8_t reg7d2val = 0x80;

    switch(eft)
    {
        case 1: reg7d0val = 0x40; break;
        case 2: reg7d0val = 0x18; break;
        case 3: reg7d0val = 0x18; reg7d1val = 0x40; reg7d2val = 0xC0; break;
        case 4: reg7d0val = 0x18; reg7d1val = 0x40; reg7d2val = 0x40; break;
        case 5: reg7d0val = 0x18; reg7d1val = 0xA0; reg7d2val = 0x40; break;
        case 6: reg7d0val = 0x18; reg7d1val = 0x40; reg7d2val = 0xA6; break;
        default: break;
    }

    SCCB_WR_Reg(0xFF, 0x00);
    SCCB_WR_Reg(0x7C, 0x00);
    SCCB_WR_Reg(0x7D, reg7d0val);
    SCCB_WR_Reg(0x7C, 0x05);
    SCCB_WR_Reg(0x7D, reg7d1val);
    SCCB_WR_Reg(0x7D, reg7d2val);
}

/*===========================================================================
 *          sw=1     
 *===========================================================================*/
void OV2640_Color_Bar(uint8_t sw)
{
    uint8_t reg;

    SCCB_WR_Reg(0xFF, 0x01);
    reg = SCCB_RD_Reg(0x12);
    reg &= ~(1 << 1);
    if(sw) reg |= (1 << 1);
    SCCB_WR_Reg(0x12, reg);
}

/*===========================================================================
 *    ô            
 *===========================================================================*/
void OV2640_Window_Set(uint16_t sx, uint16_t sy,
                       uint16_t width, uint16_t height)
{
    uint16_t endx = sx + width  / 2;
    uint16_t endy = sy + height / 2;
    uint8_t  temp;

    SCCB_WR_Reg(0xFF, 0x01);
    temp  = SCCB_RD_Reg(0x03);
    temp &= 0xF0;
    temp |= ((endy & 0x03) << 2) | (sy & 0x03);
    SCCB_WR_Reg(0x03, temp);
    SCCB_WR_Reg(0x19, (uint8_t)(sy   >> 2));
    SCCB_WR_Reg(0x1A, (uint8_t)(endy >> 2));

    temp  = SCCB_RD_Reg(0x32);
    temp &= 0xC0;
    temp |= ((endx & 0x07) << 3) | (sx & 0x07);
    SCCB_WR_Reg(0x32, temp);
    SCCB_WR_Reg(0x17, (uint8_t)(sx   >> 3));
    SCCB_WR_Reg(0x18, (uint8_t)(endx >> 3));
}

/*===========================================================================
 *        ͼ   С  DSP    ţ          4  ı   
 *===========================================================================*/
uint8_t OV2640_OutSize_Set(uint16_t width, uint16_t height)
{
    uint16_t outw, outh;
    uint8_t  temp;

    if(width  % 4) return 1;
    if(height % 4) return 2;

    outw = width  / 4;
    outh = height / 4;

    SCCB_WR_Reg(0xFF, 0x00);
    SCCB_WR_Reg(0xE0, 0x04);
    SCCB_WR_Reg(0x5A, (uint8_t)(outw & 0xFF));
    SCCB_WR_Reg(0x5B, (uint8_t)(outh & 0xFF));
    temp  = (uint8_t)((outw >> 8) & 0x03);
    temp |= (uint8_t)((outh >> 6) & 0x04);
    SCCB_WR_Reg(0x5C, temp);
    SCCB_WR_Reg(0xE0, 0x00);

    return 0;
}

/*===========================================================================
 *      DSP    봰  
 *===========================================================================*/
uint8_t OV2640_ImageWin_Set(uint16_t offx, uint16_t offy,
                            uint16_t width, uint16_t height)
{
    uint16_t hsize, vsize;
    uint8_t  temp;

    if(width  % 4) return 1;
    if(height % 4) return 2;

    hsize = width  / 4;
    vsize = height / 4;

    SCCB_WR_Reg(0xFF, 0x00);
    SCCB_WR_Reg(0xE0, 0x04);
    SCCB_WR_Reg(0x51, (uint8_t)(hsize & 0xFF));
    SCCB_WR_Reg(0x52, (uint8_t)(vsize & 0xFF));
    SCCB_WR_Reg(0x53, (uint8_t)(offx  & 0xFF));
    SCCB_WR_Reg(0x54, (uint8_t)(offy  & 0xFF));
    temp  = (uint8_t)((vsize >> 1) & 0x80);
    temp |= (uint8_t)((offy  >> 4) & 0x70);
    temp |= (uint8_t)((hsize >> 5) & 0x08);
    temp |= (uint8_t)((offx  >> 8) & 0x07);
    SCCB_WR_Reg(0x55, temp);
    SCCB_WR_Reg(0x57, (uint8_t)((hsize >> 2) & 0x80));
    SCCB_WR_Reg(0xE0, 0x00);

    return 0;
}

/*===========================================================================
 * ѡ  ͼ  ߴ磨ͬʱѡ  ֱ   ģʽ  
 *===========================================================================*/
uint8_t OV2640_ImageSize_Set(uint16_t width, uint16_t height)
{
    uint8_t temp;

    SCCB_WR_Reg(0xFF, 0x00);
    SCCB_WR_Reg(0xE0, 0x04);
    SCCB_WR_Reg(0xC0, (uint8_t)((width  >> 3) & 0xFF));
    SCCB_WR_Reg(0xC1, (uint8_t)((height >> 3) & 0xFF));
    temp  = (uint8_t)((width  & 0x07) << 3);
    temp |= (uint8_t)( height & 0x07);
    temp |= (uint8_t)((width  >> 4) & 0x80);
    SCCB_WR_Reg(0x8C, temp);
    SCCB_WR_Reg(0xE0, 0x00);

    return 0;
}
