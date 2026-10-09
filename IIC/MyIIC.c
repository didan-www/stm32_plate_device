#include "stm32f4xx.h"
#include "mydelay.h"
#include "FreeRTOS.h"
#include "task.h"

#define MYSDA_HIGH()  GPIO_SetBits(GPIOB, GPIO_Pin_9)
#define MYSDA_LOW()   GPIO_ResetBits(GPIOB, GPIO_Pin_9)
#define MYSCL_HIGH()  GPIO_SetBits(GPIOB, GPIO_Pin_8)
#define MYSCL_LOW()   GPIO_ResetBits(GPIOB, GPIO_Pin_8)

void IIC_ACK(void);
void IIC_NACK(void);

void My_IIC_GPIOInit(void)
{
        Delay_Init();
GPIO_InitTypeDef s;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    s.GPIO_Mode = GPIO_Mode_OUT;
    s.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    s.GPIO_OType = GPIO_OType_OD;
    s.GPIO_PuPd = GPIO_PuPd_UP;
    s.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOB, &s);
    GPIO_SetBits(GPIOB, GPIO_Pin_8 | GPIO_Pin_9);
}

void SDA_OUT(void)
{
    GPIO_InitTypeDef S;
    S.GPIO_Pin = GPIO_Pin_9;
    S.GPIO_Mode = GPIO_Mode_OUT;
    S.GPIO_OType = GPIO_OType_OD;
    S.GPIO_PuPd = GPIO_PuPd_UP;
    S.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOB, &S);
}

void SDA_IN(void)
{
    GPIO_InitTypeDef S;
    S.GPIO_Pin = GPIO_Pin_9;
    S.GPIO_Mode = GPIO_Mode_IN;
    S.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOB, &S);
}

uint8_t SDA_READ(void)
{
    return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_9);
}

void IIC_Start(void)
{
    SDA_OUT();
    MYSDA_HIGH();
    MYSCL_HIGH();
    Delay_us(10);
    MYSDA_LOW();
    Delay_us(10);
    MYSCL_LOW();
}

void IIC_SendByte(uint8_t data)
{
    SDA_OUT();
    for (uint8_t i = 0; i < 8; i++)
    {
        MYSCL_LOW();
        if (data & 0x80)
            MYSDA_HIGH();
        else
            MYSDA_LOW();
        data <<= 1;
        MYSCL_HIGH();
        Delay_us(10);
    }
    MYSDA_HIGH();
    MYSCL_LOW();
}

uint8_t IICWaitAck(void)
{
    SDA_IN();
    Delay_us(5);
    MYSCL_HIGH();
    if (SDA_READ() == 0)
    {
        MYSCL_LOW();
        return 0;
    }
    else
    {
        MYSCL_LOW();
        return 1;
    }
}

void IIC_Stop(void)
{
    SDA_OUT();
    MYSDA_LOW();
    MYSCL_LOW();
    Delay_us(5);
    MYSCL_HIGH();
    Delay_us(5);
    MYSDA_HIGH();
    Delay_us(5);
}

uint8_t IIC_ReadByte(uint8_t ack)
{
    uint8_t data = 0;
    SDA_IN();
    for (uint8_t i = 0; i < 8; i++)
    {
        Delay_us(10);
        MYSCL_HIGH();
        data <<= 1;
        if (SDA_READ())
            data++;
        MYSCL_LOW();
    }
    if (ack == 0)
        IIC_ACK();
    else
        IIC_NACK();
    return data;
}

void IIC_ACK(void)
{
    SDA_OUT();
    MYSDA_LOW();
    Delay_us(5);
    MYSCL_HIGH();
    Delay_us(5);
    MYSCL_LOW();
}

void IIC_NACK(void)
{
    SDA_OUT();
    MYSDA_HIGH();
    Delay_us(5);
    MYSCL_HIGH();
    Delay_us(5);
    MYSCL_LOW();
}
