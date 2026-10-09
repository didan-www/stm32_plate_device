#include "w25q64.h"


static void W25Q64_WaitBusy(void)
{
    uint8_t status;
	#if (configUSE_FREERTOS == 1)
		SPI_Lock();
	#endif
    MySPI_Start();
    MySPI_SwapByte(0x05);
    do
    {
        status = MySPI_SwapByte(0xFF);
    }while((status & 0x01U));
    MySPI_Stop();
	#if (configUSE_FREERTOS == 1)
		SPI_Unlock();
	#endif
}

void W25Q64_Init(void)
{
    MySPI_Init();
}

uint32_t W25Q64_ReadID(void)
{
    uint16_t id;
	#if (configUSE_FREERTOS == 1)
		SPI_Lock();
	#endif
    MySPI_Start();
    MySPI_SwapByte(0x90);
    MySPI_SwapByte(0x00);
    MySPI_SwapByte(0x00);
    MySPI_SwapByte(0x00);
    id  = MySPI_SwapByte(0xFF) << 8;
    id |= MySPI_SwapByte(0xFF);
    MySPI_Stop();
	#if (configUSE_FREERTOS == 1)
		SPI_Unlock();
	#endif
    return (uint32_t)id;
}

void W25Q64_Read(uint8_t *buf, uint32_t addr, uint32_t len)
{
	#if (configUSE_FREERTOS == 1)
		SPI_Lock();
	#endif
    MySPI_Start();
    MySPI_SwapByte(0x03);
    MySPI_SwapByte((addr>>16)&0xFF);
    MySPI_SwapByte((addr>>8)&0xFF);
    MySPI_SwapByte(addr&0xFF);
    MySPI_RecvBuf(buf, len);
    MySPI_Stop();
	#if (configUSE_FREERTOS == 1)
		SPI_Unlock();
	#endif
}

static void W25Q64_PageWrite(uint8_t *buf,uint32_t addr,uint32_t len)
{
	#if (configUSE_FREERTOS == 1)
		SPI_Lock();
	#endif
    MySPI_Start();
    MySPI_SwapByte(0x06);
    MySPI_Stop();
	#if (configUSE_FREERTOS == 1)
		SPI_Unlock();
	#endif

	#if (configUSE_FREERTOS == 1)
		SPI_Lock();
	#endif
    MySPI_Start();
    MySPI_SwapByte(0x02);
    MySPI_SwapByte((addr>>16)&0xFF);
    MySPI_SwapByte((addr>>8)&0xFF);
    MySPI_SwapByte(addr&0xFF);
    MySPI_SendBuf(buf, len);
    MySPI_Stop();
	#if (configUSE_FREERTOS == 1)
		SPI_Unlock();
	#endif

    W25Q64_WaitBusy();
}

void W25Q64_Write(uint8_t *buf,uint32_t addr,uint32_t len)
{
    uint32_t page_left = 256U - (addr % 256U);
    uint32_t wlen;
    while(len>0U)
    {
        wlen = len > page_left ? page_left : len;
        W25Q64_PageWrite(buf, addr, wlen);
        addr += wlen;
        buf += wlen;
        len -= wlen;
        page_left = 256U;
    }
}

void W25Q64_SectorErase(uint32_t addr)
{
	#if (configUSE_FREERTOS == 1)
		SPI_Lock();
	#endif
    MySPI_Start();
    MySPI_SwapByte(0x06);
    MySPI_Stop();
	#if (configUSE_FREERTOS == 1)
		SPI_Unlock();
	#endif

	#if (configUSE_FREERTOS == 1)
		SPI_Lock();
	#endif
    MySPI_Start();
    MySPI_SwapByte(0x20);
    MySPI_SwapByte((addr>>16)&0xFF);
    MySPI_SwapByte((addr>>8)&0xFF);
    MySPI_SwapByte(addr&0xFF);
    MySPI_Stop();
	#if (configUSE_FREERTOS == 1)
		SPI_Unlock();
	#endif

    W25Q64_WaitBusy();
}

//整片擦除，你的FatFs_Setup会调用
void W25Q64_ChipErase(void)
{
	#if (configUSE_FREERTOS == 1)
		SPI_Lock();
	#endif
    MySPI_Start();
    MySPI_SwapByte(0x06);
    MySPI_Stop();
	#if (configUSE_FREERTOS == 1)
		SPI_Unlock();
	#endif

	#if (configUSE_FREERTOS == 1)
		SPI_Lock();
	#endif
    MySPI_Start();
    MySPI_SwapByte(0xC7);
    MySPI_Stop();
	#if (configUSE_FREERTOS == 1)
		SPI_Unlock();
	#endif

    W25Q64_WaitBusy();
}