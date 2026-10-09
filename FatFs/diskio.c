/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/
#include "w25q64.h"
#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */
#include "stdio.h"
extern uint8_t W25Q64_SECTOR_BUF[FF_MAX_SS];


/* Definitions of physical drive number for each drive */
//#define DEV_RAM		0	/* Example: Map Ramdisk to physical drive 0 */
//#define DEV_MMC		1	/* Example: Map MMC/SD card to physical drive 1 */
//#define DEV_USB		2	/* Example: Map USB MSD to physical drive 2 */
#define SPI_Flash		0//设备号

/*-----------------------------------------------------------------------*/
/* 获取驱动器状态                                                    */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	if(pdrv == 0) return RES_OK;
    return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* 初始化磁盘驱动器                                               */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (BYTE pdrv)
{
    uint32_t id;
    if(pdrv == 0)
    {
        id = W25Q64_ReadID();
        if(id != W25Q64_ID)
        {
            return STA_NOINIT;
        }
        return RES_OK;
    }
    return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* 读扇区                                                       */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* 磁盘驱动号 */
	BYTE *buff,		/* 缓存区 */
	LBA_t sector,	/* 扇区号 */
	UINT count		/* 扇区个数 */
)
{
	uint32_t addr;
    if(pdrv !=0) return RES_PARERR;
    addr = sector * W25Q64_SECTOR_SIZE;
    W25Q64_Read(buff, addr, count * W25Q64_SECTOR_SIZE);
    return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* 写扇区                                                     */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* 磁盘驱动号 */
	const BYTE *buff,	/* 发送缓冲区 */
	LBA_t sector,		/* 扇区号 */
	UINT count			/* 扇区个数 */
)
{
uint32_t addr,i;
    if(pdrv !=0) return RES_PARERR;
    addr = sector * W25Q64_SECTOR_SIZE;

    for(i=0;i<count;i++)
    {
        W25Q64_SectorErase(addr + i*W25Q64_SECTOR_SIZE);
        W25Q64_Write((uint8_t*)buff + i*W25Q64_SECTOR_SIZE, addr + i*W25Q64_SECTOR_SIZE, W25Q64_SECTOR_SIZE);
    }
    return RES_OK;
}

#endif


/*-----------------------------------------------------------------------*/
/* 格式化w25q64/应用层获取容量                                       */
/*-----------------------------------------------------------------------*/

//DRESULT disk_ioctl (
//	BYTE pdrv,		/* Physical drive nmuber (0..) */
//	BYTE cmd,		/* Control code */
//	void *buff		/* Buffer to send/receive control data */
//)
//{
//	DWORD *dw;
//    if (pdrv != 0) return RES_PARERR;
//    dw = buff;
//    switch(cmd)
//    {
//        case GET_SECTOR_COUNT:  *dw = (8U*1024U*1024U)/ W25Q64_SECTOR_SIZE; break;
//        case GET_SECTOR_SIZE:   *dw = W25Q64_SECTOR_SIZE; break;
//        case GET_BLOCK_SIZE:    *dw = 1U; break;
//        default: return RES_PARERR;
//    }
//    return RES_OK;
//}

DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void *buff)
{
    DWORD *dw;
    if (pdrv != 0) return RES_PARERR;
    dw = buff;
    switch(cmd)
    {
        case CTRL_SYNC:        return RES_OK;   // ← 加这行
        case GET_SECTOR_COUNT: *dw = (8U*1024U*1024U) / W25Q64_SECTOR_SIZE; break;
        case GET_SECTOR_SIZE:  *dw = W25Q64_SECTOR_SIZE; break;
        case GET_BLOCK_SIZE:   *dw = 1U; break;
        default: return RES_PARERR;
    }
    return RES_OK;
}