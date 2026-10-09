#include "test.h"
#include "my_spi.h"
#include "w25q64.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>


DWORD get_fattime(void)
{
    return ((DWORD)(2026 - 1980) << 25)
         | ((DWORD)8 << 21)
         | ((DWORD)15 << 16)
         | ((DWORD)12 << 11)
         | ((DWORD)0 << 5);
}

void SystemClock_Config(void)
{
    RCC_DeInit();
    RCC_HSICmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET);
    RCC_PLLConfig(RCC_PLLSource_HSI, 16, 336, 2, 7);
    RCC_PLLCmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div4);
    RCC_PCLK2Config(RCC_HCLK_Div2);
    SystemCoreClockUpdate();
}

void Test_SPI_Flash(void)
{
    uint8_t manuf, memtype, cap;
    uint32_t id;

    printf("\r\n=== [1] SPI Raw Test ===\r\n");
    W25Q64_Init();

    MySPI_Start();
    MySPI_SwapByte(0x9F);
    manuf   = MySPI_SwapByte(0xFF);
    memtype = MySPI_SwapByte(0xFF);
    cap     = MySPI_SwapByte(0xFF);
    MySPI_Stop();
    printf("JEDEC ID: %02X %02X %02X\r\n", manuf, memtype, cap);

    id = W25Q64_ReadID();
    printf("Device ID: 0x%04X (expect 0x%04X) -> %s\r\n",
           id, W25Q64_ID, (id == W25Q64_ID) ? "OK" : "FAIL");
}

void Test_W25Q64(void)
{
    #define TEST_ADDR  0x000000
    #define TEST_LEN   256

    uint8_t wbuf[TEST_LEN];
    uint8_t rbuf[TEST_LEN];
    uint32_t i;
    uint8_t fail;

    printf("\r\n=== [2] W25Q64 R/W Test ===\r\n");

    if (W25Q64_ReadID() != W25Q64_ID)
    {
        printf("W25Q64 not found, skip.\r\n");
        return;
    }

    for (i = 0; i < TEST_LEN; i++)
        wbuf[i] = (uint8_t)(i & 0xFF);

    printf("Erasing sector @ 0x%06X...\r\n", TEST_ADDR);
    W25Q64_SectorErase(TEST_ADDR);

    W25Q64_Read(rbuf, TEST_ADDR, TEST_LEN);
    fail = 0;
    for (i = 0; i < TEST_LEN; i++)
        if (rbuf[i] != 0xFF) { fail = 1; break; }
    printf("After erase: %s\r\n", fail ? "FAIL" : "OK (all 0xFF)");

    printf("Writing %u bytes...\r\n", TEST_LEN);
    W25Q64_Write(wbuf, TEST_ADDR, TEST_LEN);

    memset(rbuf, 0, TEST_LEN);
    W25Q64_Read(rbuf, TEST_ADDR, TEST_LEN);
    fail = 0;
    for (i = 0; i < TEST_LEN; i++)
        if (rbuf[i] != wbuf[i]) { fail = 1; break; }
    printf("Readback: %s", fail ? "FAIL" : "OK");
    if (fail)
        printf(" @ byte %u: w=0x%02X r=0x%02X", i, wbuf[i], rbuf[i]);
    printf("\r\n");

    #undef TEST_ADDR
    #undef TEST_LEN
}

uint8_t Test_FatFs(void)
{
    static FATFS fs;
    static FIL fil;
    static uint8_t work[FF_MAX_SS];
    MKFS_PARM opt;
    FRESULT res;
    UINT bw, br;
    const char *wstr = "FatFs on W25Q64 - PASS!\r\n";
    char rbuf[64];

    printf("\r\n=== [3] FatFs Test ===\r\n");

    res = f_mount(&fs, "0:", 1);
    if (res == FR_NO_FILESYSTEM)
    {
        printf("No filesystem, formatting...\r\n");
        opt.fmt     = FM_FAT | FM_SFD;
        opt.n_fat   = 1;
        opt.align   = 0;
        opt.n_root  = 0;
        opt.au_size = 0;
        res = f_mkfs("0:", &opt, work, sizeof(work));
        if (res != FR_OK)
        {
            printf("f_mkfs error: %d\r\n", res);
            return 1;
        }
        f_mount(NULL, "0:", 0);
        res = f_mount(&fs, "0:", 1);
    }
    if (res != FR_OK)
    {
        printf("f_mount error: %d\r\n", res);
        return 1;
    }
    printf("Mount OK\r\n");

    res = f_open(&fil, "0:test.txt", FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK) { printf("f_open(w) err: %d\r\n", res); return 1; }
    res = f_write(&fil, wstr, strlen(wstr), &bw);
    f_sync(&fil);
    f_close(&fil);
    printf("Write: %u bytes (res=%d)\r\n", bw, res);

    res = f_open(&fil, "0:test.txt", FA_READ);
    if (res != FR_OK) { printf("f_open(r) err: %d\r\n", res); return 1; }
    memset(rbuf, 0, sizeof(rbuf));
    res = f_read(&fil, rbuf, sizeof(rbuf) - 1, &br);
    f_close(&fil);
    printf("Read: %u bytes (res=%d)\r\n", br, res);
    printf("Content: %s", rbuf);

    if (strcmp(rbuf, wstr) == 0)
        printf(">>> FatFs Test: PASS\r\n");
    else
        printf(">>> FatFs Test: FAIL (mismatch)\r\n");

    return 0;
}
