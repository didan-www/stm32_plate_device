/*===========================================================================
 * sccb.c
 *     ģ   SCCB Э  
 * PD6 = SCL    PD7 = SDA
 *===========================================================================*/

#include "Sccb.h"
#include "mydelay.h"

void SCCB_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;
    gpio.GPIO_Mode  = GPIO_Mode_OUT;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOD, &gpio);

    GPIO_SetBits(GPIOD, GPIO_Pin_6 | GPIO_Pin_7);
    SCCB_SDA_OUT();
}

void SCCB_Start(void)
{
    SCCB_SDA(1);
    SCCB_SCL(1);
    MyDelay_us(50);
    SCCB_SDA(0);
    MyDelay_us(50);
    SCCB_SCL(0);
}

void SCCB_Stop(void)
{
    SCCB_SDA(0);
    MyDelay_us(50);
    SCCB_SCL(1);
    MyDelay_us(50);
    SCCB_SDA(1);
    MyDelay_us(50);
}

void SCCB_No_Ack(void)
{
    MyDelay_us(50);
    SCCB_SDA(1);
    SCCB_SCL(1);
    MyDelay_us(50);
    SCCB_SCL(0);
    MyDelay_us(50);
    SCCB_SDA(0);
    MyDelay_us(50);
}

/*      0= ɹ   1=   ACK */
uint8_t SCCB_WR_Byte(uint8_t dat)
{
    uint8_t j, res;

    for(j = 0; j < 8; j++)
    {
        SCCB_SDA((dat & 0x80) ? 1 : 0);
        dat <<= 1;
        MyDelay_us(50);
        SCCB_SCL(1);
        MyDelay_us(50);
        SCCB_SCL(0);
    }

    SCCB_SDA_IN();
    MyDelay_us(50);
    SCCB_SCL(1);
    MyDelay_us(50);
    res = SCCB_READ_SDA ? 1 : 0;
    SCCB_SCL(0);
    SCCB_SDA_OUT();

    return res;
}

uint8_t SCCB_RD_Byte(void)
{
    uint8_t temp = 0, j;

    SCCB_SDA_IN();
    for(j = 8; j > 0; j--)
    {
        MyDelay_us(50);
        SCCB_SCL(1);
        temp <<= 1;
        if(SCCB_READ_SDA) temp++;
        MyDelay_us(50);
        SCCB_SCL(0);
    }
    SCCB_SDA_OUT();

    return temp;
}

uint8_t SCCB_WR_Reg(uint8_t reg, uint8_t data)
{
    uint8_t res = 0;

    SCCB_Start();
    if(SCCB_WR_Byte(SCCB_ID)) res = 1;
    MyDelay_us(100);
    if(SCCB_WR_Byte(reg))     res = 1;
    MyDelay_us(100);
    if(SCCB_WR_Byte(data))    res = 1;
    SCCB_Stop();

    return res;
}

uint8_t SCCB_RD_Reg(uint8_t reg)
{
    uint8_t val = 0;

    /* SCCB    У д  Ĵ     ַ     Stop         Restart */
    SCCB_Start();
    SCCB_WR_Byte(SCCB_ID);
    MyDelay_us(100);
    SCCB_WR_Byte(reg);
    MyDelay_us(100);
    SCCB_Stop();

    MyDelay_us(100);

    SCCB_Start();
    SCCB_WR_Byte(SCCB_ID | 0x01);
    MyDelay_us(100);
    val = SCCB_RD_Byte();
    SCCB_No_Ack();
    SCCB_Stop();

    return val;
}
