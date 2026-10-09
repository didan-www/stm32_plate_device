#include "mydelay.h"
#include "FreeRTOS.h"
#include "task.h"

void Delay_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0;
}

void Delay_us(uint32_t us)
{
	//CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  // 使能 DWT 模块
	//DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;             // 启动 CYCCNT 计数
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks);
}

void Delay_ms(uint32_t ms)
{
   while(ms--)
   {
	   Delay_us(1000);
   }
}

void MyDelay_us(uint32_t us)
{
    Delay_us(us);
}

void MyDelay_ms(uint32_t ms)
{
    Delay_ms(ms);
}
