/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#ifndef FREERTOS_CONFIG_H 
#define FREERTOS_CONFIG_H

#ifndef __ICCARM__
#define __ICCARM__
	#include <stdint.h>
	extern uint32_t SystemCoreClock;
#endif

/**********************************************
 *配置抢占式调度器。0 为协程，已经不做更新了
 *时间片调度：configUSE_TIME_SLICING
 *********************************************/
#define configUSE_PREEMPTION 1  //抢占调度
/**********************************************
 *空闲钩子函数的使用与否
 *********************************************/
#define configUSE_IDLE_HOOK 0
/**********************************************
 *时间片钩子函数
 *********************************************/
#define configUSE_TICK_HOOK 0

/**********************************************
 *写入CPU内核的时钟频率
 *********************************************/
#define configCPU_CLOCK_HZ  ( SystemCoreClock )

/**********************************************
 *每秒产生中断的次数
 *********************************************/
#define configTICK_RATE_HZ  ( ( TickType_t ) 1000 )

/**********************************************
 *可使用的最大优先级
 *********************************************/
#define configMAX_PRIORITIES    (10)
/**********************************************
 *空闲任务使用的栈大小 单位为字
 *********************************************/
#define configMINIMAL_STACK_SIZE    ( ( unsigned short ) 130 )

#define configTOTAL_HEAP_SIZE   ( ( size_t ) ( 80* 1024 ) )  //*系统所有堆的大小

#define configMAX_TASK_NAME_LEN ( 20 )  //任务名的字符串长度
#define configUSE_TRACE_FACILITY    1

#define configUSE_16_BIT_TICKS          0 //系统节拍定时器的变量类型，1为uint16_t ,0为uin32_t
#define configIDLE_SHOULD_YIELD         1 //空闲任务放弃CPU的使用权，交给其他同优先级的用户任务
#define configUSE_MUTEXES               1 //使用互斥信号量
#define configQUEUE_REGISTRY_SIZE       8 //信号量和消息队列的个数
#define configCHECK_FOR_STACK_OVERFLOW  0
#define configUSE_RECURSIVE_MUTEXES     1 //使用递归互斥信号量
#define configUSE_MALLOC_FAILED_HOOK    0 //内存申请失败函数
#define configUSE_APPLICATION_TASK_TAG  0
#define configUSE_COUNTING_SEMAPHORES   1 //*为1时，使用计数信号量
#define configGENERATE_RUN_TIME_STATS   0

#define configSUPPORT_STATIC_ALLOCATION    0  //静态创建任务
#define configSUPPORT_DYNAMIC_ALLOCATION   1  //支持动态内存申请
//#define configUSE_HEAP_SCHEME 4

/* Software timer definitions. */
#define configUSE_TIMERS        0
#define configTIMER_TASK_PRIORITY   ( 2 )
#define configTIMER_QUEUE_LENGTH        10
#define configTIMER_TASK_STACK_DEPTH    ( configMINIMAL_STACK_SIZE * 2 )

/* Set the following definitions to 1 to include the API function, or zero
to exclude the API function. */
#define INCLUDE_vTaskPrioritySet        1
#define INCLUDE_uxTaskPriorityGet       1
#define INCLUDE_vTaskDelete             1
#define INCLUDE_vTaskCleanUpResources   1
#define INCLUDE_vTaskSuspend            1
#define INCLUDE_vTaskDelayUntil         1
#define INCLUDE_vTaskDelay              1

//中断配置
/* Cortex-M specific definitions. */
#ifdef __NVIC_PRIO_BITS     //使用多少位作为中断优先级
	/* __BVIC_PRIO_BITS will be specified when CMSIS is being used. */
	#define configPRIO_BITS         __NVIC_PRIO_BITS
#else
	#define configPRIO_BITS         4        /* 15 priority levels */
#endif

/* The lowest interrupt priority that can be used in a call to a "set priority"
function. */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY     0xf  //中断的最低优先级为15

/* 最大的系统中断优先级为5，表示低于数值5的优先级不受FreeRTOS的管理，不可以被屏蔽。 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5  

/* Interrupt priorities used by the kernel port layer itself.  These are generic
to all Cortex-M ports, and do not rely on any particular library functions. */
#define configKERNEL_INTERRUPT_PRIORITY     ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
/* !!!! configMAX_SYSCALL_INTERRUPT_PRIORITY must not be set to zero !!!!
See http://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html. */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* Normal assert() semantics without relying on the provision of an assert.h
header file. */
#define configASSERT( x ) if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

/* Definitions that map the FreeRTOS port interrupt handlers to their CMSIS
standard names. */
#define vPortSVCHandler SVC_Handler   //中断向量表和rtos中定义的中断处理函数名字不同。
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
#define INCLUDE_xTaskGetSchedulerState    1
#endif /* FREERTOS_CONFIG_H */

