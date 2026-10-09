#include "stm32f4xx.h"                  // Device header
#include "my_spi.h"

#if(configUSE_FREERTOS==1)
#includ "FREERTOS.h"
#include "semphr.h"
SemaphoreHandle_t spi_mutex;
#endif

#define SPI_CS_H()	(SPI_GPIO_PORT->BSRRL=SPI_PIN_CS)//BSRRL:置1寄存器，写1对应引脚输出高
#define SPI_CS_L()	(SPI_GPIO_PORT->BSRRH=SPI_PIN_CS)//BSRRH:置0寄存器，写1对应引脚输出低


void MySPI_Init(void)
{
	/* 开启GPIO、SPI2外设时钟 */
	RCC_AHB1PeriphClockCmd(SPI_GPIO_CLK,ENABLE);
	RCC_APB1PeriphClockCmd(SPIx_CLK,ENABLE);
	/* ========== CS PB12：普通推挽输出 软件片选 ========== */
	GPIO_InitTypeDef s;
	s.GPIO_Mode=GPIO_Mode_OUT;
	s.GPIO_OType=GPIO_OType_PP;
	s.GPIO_Pin=SPI_PIN_CS;
	s.GPIO_Speed=GPIO_Speed_100MHz;
	s.GPIO_PuPd=GPIO_PuPd_UP;
	GPIO_Init(SPI_GPIO_PORT,&s);
	/* ========== SCK MISO MOSI 设置为复用AF模式 ========== */
	GPIO_InitTypeDef s1;
	s1.GPIO_Mode=GPIO_Mode_AF;
	s1.GPIO_OType=GPIO_OType_PP;
	s1.GPIO_Speed=GPIO_Speed_100MHz;
	s1.GPIO_PuPd=GPIO_PuPd_UP;
	s1.GPIO_Pin=SPI_PIN_SCK | SPI_PIN_MOSI | SPI_PIN_MISO;
	GPIO_Init(SPI_GPIO_PORT,&s1);
	/* 引脚映射到SPI2 AF5复用功能 */
    GPIO_PinAFConfig(SPI_GPIO_PORT, SPI_SRC_SCK,  SPI_AF);
    GPIO_PinAFConfig(SPI_GPIO_PORT, SPI_SRC_MISO, SPI_AF);
    GPIO_PinAFConfig(SPI_GPIO_PORT, SPI_SRC_MOSI, SPI_AF);
	/* ========= SPI外设参数配置 ========= */
	SPI_InitTypeDef s2;
	s2.SPI_Direction=SPI_Direction_2Lines_FullDuplex;
	s2.SPI_Mode=SPI_Mode_Master;
	s2.SPI_DataSize=SPI_DataSize_8b;
	s2.SPI_CPOL=SPI_CPOL_Low;//模式0 空闲SCK低电平
	s2.SPI_CPHA=SPI_CPHA_1Edge;//第一个边沿采样
	s2.SPI_NSS=SPI_NSS_Soft;//软件NSS
	s2.SPI_BaudRatePrescaler=SPI_BaudRatePrescaler_4;
	s2.SPI_FirstBit=SPI_FirstBit_MSB;
	s2.SPI_CRCPolynomial=7;
	SPI_Init(SPIx,&s2);
	/* 软件NSS主机必须SSI置1，防止MODF模式故障 */
    SPI_NSSInternalSoftwareConfig(SPIx, SPI_NSSInternalSoft_Set);

    SPI_Cmd(SPIx, ENABLE);
    SPI_CS_H(); //初始化默认片选拉高

	#if (configUSE_FREERTOS == 1)
		spi_mutex = xSemaphoreCreateMutex();
	#endif
}
//选中从设备，CS拉低，开启一次SPI会话
void MySPI_Start(void)
{
    SPI_CS_L();
}

//结束SPI会话，CS拉高
void MySPI_Stop(void)
{
    SPI_CS_H();
}

//交换1字节：发一字节同时收一字节
uint8_t MySPI_SwapByte(uint8_t ByteSend)
{
    while((SPIx->SR & SPI_I2S_FLAG_TXE) == 0);
    SPIx->DR = ByteSend;

    while((SPIx->SR & SPI_I2S_FLAG_RXNE) == 0);
    return (uint8_t)SPIx->DR;
}

//发送缓冲区，丢弃收到的数据
void MySPI_SendBuf(const uint8_t *buf, uint32_t len)
{
    while(len--)
    {
        while((SPIx->SR & SPI_I2S_FLAG_TXE) == 0);
        SPIx->DR = *buf++;
        while((SPIx->SR & SPI_I2S_FLAG_RXNE) == 0);
        (void)SPIx->DR;
    }
}

//接收缓冲区，发送dummy 0XFF产生时钟
void MySPI_RecvBuf(uint8_t *buf, uint32_t len)
{
    while(len--)
    {
        while((SPIx->SR & SPI_I2S_FLAG_TXE) == 0);
        SPIx->DR = 0xFF;
        while((SPIx->SR & SPI_I2S_FLAG_RXNE) == 0);
        *buf++ = (uint8_t)SPIx->DR;
    }
}

#if (configUSE_FREERTOS == 1)
void SPI_Lock(void)
{
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
}

void SPI_Unlock(void)
{
    xSemaphoreGive(spi_mutex);
}
#endif