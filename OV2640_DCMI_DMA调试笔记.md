# STM32F407 OV2640 DCMI DMA 采集失败调试笔记

> **项目背景**：STM32F407VE + FreeRTOS 智能停车场项目，OV2640 摄像头 JPEG 采集，通过 DCMI + DMA2_Stream1 双缓冲传输，最终经 ESP01 WiFi/MQTT/HTTP 上传照片。
>
> **核心现象**：DCMI 启动后 DMA 几乎不传输数据，NDTR 只从 1024 降到 1020（仅 4 word = 16 字节），3 次重试均失败，`frame too small: 0`。
>
> **最终根因**：DMA 缓冲区被链接器分配到了 CCM RAM（0x10000000），而 DMA2 无法访问 CCM，传输静默失败。

---

## 一、发现问题

### 1.1 初始故障现象

系统启动后，各任务初始化正常：

```
I/elog   EasyLogger V2.2.99 is initialize success.
I/main   System Boot
I/app.c  Uplink_Queue OK
I/app.c  Downlink_Queue OK
I/app.c  Event_Queue OK
I/ov2640 OV2640 ID=0x2642 OK
I/ov2640 OV2640 init OK
I/camera OV2640 JPEG 640x480 ready
I/dcmi   DCMI init OK (continuous, VSPol_Low, HSPol_High, PCK_Falling)
```

SRF05 超声波触发拍照后，DCMI 开始采集，但每次都失败：

```
I/camera === attempt 1 start ===
I/camera step1: DCMI_Stop
I/camera step1.5: My_DCMI_Init
I/camera step2: DCMI_DMA_Init
I/camera step3: DCMI_Start
I/dcmi   DCMI_Start: CR=0x00004141 SR=0x00000000 DMA_CR=0x02065511 NDTR=1024
I/camera capture: total_bytes=0
I/camera capture: frame too small, retry
...（3 次后）
E/camera (553 Camera_Task)capture failed after 3 attempts
```

**关键数据点**：
- `DCMI_Start` 时 `NDTR=1024`（DMA 缓冲区大小，单位 word）
- 采集结束后 `total_bytes=0`
- 调试打印显示 `NDTR=1020`（只减少了 4，即传输了 4 个 word = 16 字节）
- `FNE=0`（DCMI FIFO 为空）

### 1.2 排除硬件问题

用户使用同一套板子和外设，烧录别人的参考代码，能够正常拍照并上传。因此**确认硬件无问题**，故障在软件配置。

---

## 二、分析问题（排查过程与弯路）

### 2.1 弯路一：DCMI 极性穷举

首先怀疑 DCMI 同步信号极性配置错误。DCMI 有三个极性可配：
- **PCKPOL**：PCLK 采样沿（Rising / Falling）
- **HSPOL**：HSYNC/HREF 有效电平（Low / High）
- **VSPOL**：VSYNC 有效电平（Low / High）

依次测试了 PCLK(Rising/Falling) × HSYNC(Low/High) 四种组合，每种组合下 `NDTR` 都只降到 1020，`FNE=0`，`frame too small: 0`。

> **结论**：极性不是根本原因，或者测试方法有问题（后来证明测试被一行强制代码污染了，见 2.2）。

### 2.2 弯路二：库函数 PCKPOL 疑似失效 + 强制覆盖

在调试中发现，代码中设置 `DCMI_PCKPolarity_Falling`，但读回 `DCMI->CR` 显示 bit8=0（仍是 Rising）。怀疑标准外设库的 `DCMI_Init` 没有正确设置 PCKPOL。

于是在 `DCMI_Start()` 中加了一行强制写：

```c
DCMI->CR |= (1 << 8);   // 强制 PCLK Falling
```

加了之后 CR 变为 `0x4101`（bit8=1），确认 Falling 生效。

> **⚠️ 这行代码后来被证明是灾难性的**：它在每次 `DCMI_Start()` 时无条件把 PCKPOL 设为 Falling，覆盖了 `DCMI_Init()` 中的配置。导致后续所有" Rising 极性测试"实际上都被改成了 Falling，极性穷举完全无效。

### 2.3 弯路三：DCMI_CR 位定义误解

早期混淆了 DCMI_CR 的位定义：
- 误以为 bit0=ENABLE、bit1=CAPTURE
- 实际 STM32F4 DCMI_CR：**bit0=CAPTURE**、bit1=CM（连续/快照）、**bit14=ENABLE**、bit8=PCKPOL、bit9=HSPOL、bit10=VSPOL

曾用错误掩码 `DCMI->CR = (CR & ~0x1003) | 0x1003`，导致 bit1=1（快照模式）和 bit12=1（FCRC 每 2 帧），这是早期采集异常的原因之一。后改回库函数 `DCMI_Cmd(ENABLE) + DCMI_CaptureCmd(ENABLE)`。

### 2.4 弯路四：中断上下文调用日志导致死锁

在 DCMI_IRQHandler 和 DMA2_Stream1_IRQHandler 中调用了 `log_i()` / `log_e()`，导致 EasyLogger 在中断上下文死锁，系统卡死。

> **修复**：中断中只操作全局变量/标志位，日志打印放到任务上下文。

### 2.5 弯路五：OV2640 初始化问题

最初 OV2640 读 ID 失败，MID mismatch（0x74A2 expect 0x7FA2）。

**根因**：软复位（写 0x12=0x80）后，OV2640 寄存器组选择寄存器 0xFF 被重置，需要重新写 `0xFF=0x01` 切回 Sensor 寄存器组，才能正确读 MID/PID。

修复后 ID=0x7FA22642 读取成功。

### 2.6 弯路六：怀疑 PCLK 未输出 — GPIO 轮询检测

为了确认 OV2640 是否输出 PCLK，写了一个简单的电平跳变计数器：把 PA6 临时配成 GPIO 输入，统计 10ms 内电平变化次数。

```c
GPIOA->MODER &= ~(3u << (6*2));   /* PA6 设为输入 */
last = (GPIOA->IDR >> 6) & 1;
tick_start = xTaskGetTickCount();
while(xTaskGetTickCount() - tick_start < 10) {
    cur = (GPIOA->IDR >> 6) & 1;
    if(cur != last) { transitions++; last = cur; }
}
log_i("PCLK detect: PA6 transitions in 10ms = %lu", transitions);
```

**结果**：`transitions = 290`，即 10ms 内 290 次跳变 ≈ **14.5 kHz**。

正常 OV2640 PCLK 应为 24MHz，10ms 内应约 48 万次跳变。14.5kHz 恰好接近行频（HSYNC），说明 PA6 上检测到的是 HSYNC 的串扰，**PCLK 似乎没有正常输出**。

### 2.7 弯路七：怀疑 XCLK 未提供

基于 PCLK 检测结果，怀疑 OV2640 模块没有自带晶振，需要 MCU 通过 MCO 引脚提供 XCLK。于是配置 PA8 为 MCO1 输出 HSE 时钟：

```c
GPIO_PinAFConfig(GPIOA, GPIO_PinSource8, GPIO_AF_MCO);
RCC_MCO1Config(RCC_MCO1Source_HSE, RCC_MCO1Div_1);
```

但用户确认硬件接线中**没有 XCLK 引脚**，模块自带晶振。MCO 配置无效。

### 2.8 弯路八：OV2640 时钟寄存器回读

回读 OV2640 关键寄存器：

```
OV2640 reg: 0x11=0x00 0x15=0x00 0x6B=0x01 0x0D=0x87
```

- `0x11`(CLKRC) = 0x00：时钟不分频 ✅
- `0x15`(COM10) = 0x00：HSYNC/VSYNC/PCLK 无反转 ✅
- `0x6B`(DBLV) = 0x01（后又读到 0x80）：PLL/PCLK 分频，值不稳定 ❓
- `0x0D`(COM8) = 0x87 ✅

显式强制写 `0x6B=0x00`（PLL 旁路）后回读，问题依旧。

### 2.9 弯路九：GPIO AF 配置验证

怀疑 PA6 没有正确配置为 DCMI 复用功能。在 `DCMI_Start()` 中加入 GPIO 寄存器打印：

```c
log_i("GPIO: PA6 MODER=0x%08lX AFR_L=0x%08lX (PA6_AF=%lu)",
      GPIOA->MODER, GPIOA->AFR[0],
      (GPIOA->AFR[0] >> 24) & 0x0F);
```

**结果**：`PA6_AF=13`（DCMI），MODER 对应位为 `10`（AF 模式）。GPIO 配置正确。

同时排查了工程中是否有其他地方复用 PA6（如 TIM3_CH1、LED），确认 `myled.c` 虽然定义了 PA6 为 LED，但 `MyLedInit()` 从未被调用，不构成冲突。

### 2.10 关键观察汇总

到这一步，所有"表面正常"的配置都验证过了：

| 检查项 | 结果 |
|--------|------|
| OV2640 SCCB 通信 | ✅ ID 读取正常 |
| OV2640 时钟寄存器 | ✅ 配置值正确 |
| PA6 GPIO AF 配置 | ✅ AF13 (DCMI) |
| DCMI CR 寄存器 | ✅ ENABLE=1, CAPTURE=1 |
| DCMI SR 寄存器 | ⚠️ HSYNC/VSYNC 有效，但 FNE=0 |
| DMA NDTR | ❌ 只从 1024 降到 1020（4 word） |
| DMA 中断 | ❌ 几乎不进 TC 中断 |
| DCMI 帧中断 | ❌ 不进 FRAME 中断 |

**矛盾点**：DCMI 使能了、同步信号有效了、DMA 配置了，但数据就是不流动。NDTR 只动了 4 个 word——这恰好是 DMA FIFO Full 阈值（4 word），像是 DMA 启动时搬了一次就再也没有数据了。

---

## 三、解决问题

### 3.1 真正的根因

用户最终定位到问题：**DMA 缓冲区被链接器分配到了 CCM RAM，而 DMA2 无法访问 CCM。**

#### STM32F407 内存架构

STM32F407 有两块独立的 SRAM：

| 内存区域 | 起始地址 | 大小 | 总线 | CPU 访问 | DMA 访问 |
|----------|----------|------|------|----------|----------|
| 普通 SRAM | `0x20000000` | 128KB | System bus | ✅ | ✅ |
| CCM RAM | `0x10000000` | 64KB | D-bus | ✅ | ❌ |

**CCM（Core Coupled Memory）** 是紧耦合内存，直接挂在 CPU 的 D-bus 上，访问速度快（零等待），但**所有 DMA（DMA1、DMA2、以太网 DMA 等）都无法访问**。

当 DMA 试图向 CCM 地址写入时：
- 写操作被总线矩阵拒绝
- DMA 产生传输错误（Transfer Error, TE）
- 如果没有使能 TE 中断，**故障静默发生**，没有任何日志
- DMA NDTR 停止递减（或只在 FIFO 中残留少量数据）
- DCMI FIFO 数据无人搬走，持续溢出
- DCMI 帧中断永远不触发（一帧数据没搬完）

#### 为什么之前没发现

1. **NDTR 还动了 4 个 word**：这 4 个 word 是 DMA 启动时从 DCMI FIFO 中预取的数据，写入目标地址失败后残留在 DMA FIFO 中，看起来像是"传了一点就停了"。
2. **没有 TE 中断**：DMA 只使能了 TC（传输完成）中断，没有使能 TE（传输错误）中断，错误被完全掩盖。
3. **DCMI OVF 中断也没使能**：FIFO 溢出也没有任何提示。
4. **map 文件没仔细看**：如果早期查看 `.map` 文件中 `s_dma_buf0` 的地址，会发现它在 `0x1000xxxx`（CCM），问题立刻暴露。

### 3.2 修复方案

#### 第一步：给 DMA 缓冲区指定段属性

在 `Camer_task.c` 中，给三个 DMA 相关缓冲区加上 `section` 属性：

```c
#define JPEG_DMA_BUF_WORDS    (1  * 1024)   /* 4KB */
#define JPEG_DATA_BUF_WORDS   (10 * 1024)   /* 40KB */

/* DMA2只能访问普通SRAM，不能访问CCM。通过独立段配合scatter文件固定到SRAM。 */
static uint32_t __attribute__((section("DMA_SRAM"), aligned(4)))
    s_dma_buf0[JPEG_DMA_BUF_WORDS];
static uint32_t __attribute__((section("DMA_SRAM"), aligned(4)))
    s_dma_buf1[JPEG_DMA_BUF_WORDS];
static uint32_t __attribute__((section("DMA_SRAM"), aligned(4)))
    s_jpeg_data_buf[JPEG_DATA_BUF_WORDS];
```

- `s_dma_buf0 / s_dma_buf1`：DMA 双缓冲，各 4KB
- `s_jpeg_data_buf`：JPEG 汇总缓冲，40KB

#### 第二步：scatter 文件中定位段

在 `FreeRTOS_OpenCV.sct` 中，把 `DMA_SRAM` 段显式放到普通 SRAM（0x20000000）：

```
; STM32F407 scatter file
; DMA buffers must stay in SRAM; DMA2 cannot access CCM at 0x10000000.

LR_IROM1 0x08000000 0x00080000  {
  ER_IROM1 0x08000000 0x00080000  {
    *.o (RESET, +First)
    *(InRoot$$Sections)
    .ANY (+RO)
    .ANY (+XO)
  }

  RW_IRAM1 0x20000000 0x00020000  {
    camer_task.o(DMA_SRAM)    ; DMA 缓冲区必须放在普通 SRAM
    .ANY (+RW +ZI)
  }

  ; CCM is reserved for CPU-only data (FreeRTOS 栈、普通变量等)
  RW_IRAM2 0x10000000 0x00010000  {
    .ANY (+RW +ZI)
  }
}
```

关键点：
- `RW_IRAM1` 在 `0x20000000`（普通 SRAM，128KB），首先放 `camer_task.o(DMA_SRAM)`
- `RW_IRAM2` 在 `0x10000000`（CCM，64KB），放其余 RW/ZI 数据
- scatter 文件中**先声明的段优先分配**，所以 `DMA_SRAM` 一定落在普通 SRAM

#### 第三步：验证

修复后，在 `DCMI_Start` 中打印缓冲区地址：

```c
log_i("camera buffers: dma0=0x%08lX dma1=0x%08lX jpeg=0x%08lX",
      (unsigned long)s_dma_buf0,
      (unsigned long)s_dma_buf1,
      (unsigned long)s_jpeg_data_buf);
```

确认三个地址都在 `0x2000xxxx` 范围（普通 SRAM），而非 `0x1000xxxx`（CCM）。

烧录后，DCMI 采集正常，NDTR 持续递减，DMA TC 中断和 DCMI FRAME 中断正常触发，JPEG 数据成功采集并保存到 Flash。

---

## 四、经验总结

### 4.1 调试方法论反思

| 阶段 | 做了什么 | 问题 |
|------|----------|------|
| 极性穷举 | 测了 4 种组合 | 被 `DCMI->CR |= (1<<8)` 污染，Rising 从未真正生效 |
| 寄存器回读 | 读了 DCMI CR/SR、GPIO AFR、OV2640 寄存器 | 都正常，排除了这些方向 |
| PCLK 检测 | GPIO 轮询测频率 | 14.5kHz 是 HSYNC 串扰，误导了方向 |
| 中断日志 | 加了 log_i | 导致 EasyLogger 死锁，引入新问题 |
| **最终** | **看 map 文件 / scatter 文件** | **一眼定位到 CCM 问题** |

**核心教训**：当外设 + DMA 不工作时，**第一时间检查 DMA 缓冲区的链接地址**。如果地址在 `0x1000xxxx`（CCM），立刻就能定位问题，不需要绕一大圈。

### 4.2 STM32F4 DMA 开发 Checklist

每次涉及 DMA 的项目，初始化后必须确认：

1. **缓冲区地址在普通 SRAM**：`0x20000000 ~ 0x2001FFFF`，绝不能在 CCM `0x10000000 ~ 0x1000FFFF`
2. **缓冲区对齐**：根据外设数据宽度和 burst 配置，确保 4 字节或 8 字节对齐
3. **DMA 时钟使能**：`RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMAx, ENABLE)`
4. **DMA 流配置正确**：Channel、方向、数据宽度、增量模式、FIFO 阈值
5. **使能错误中断**：至少使能 TE（传输错误）中断，故障不会静默
6. **NVIC 优先级**：DMA 中断优先级不能低于 `configMAX_SYSCALL_INTERRUPT_PRIORITY`（如果在中断中用 FreeRTOS API）

### 4.3 CCM 使用建议

- ✅ **适合放 CCM**：FreeRTOS 任务栈、全局变量、CPU 密集型运算的临时缓冲区
- ❌ **不能放 CCM**：DMA 缓冲区（串口/SPI/DCMI/ADC 等）、以太网描述符、USB 缓冲区

### 4.4 快速验证 DMA 缓冲区位置的方法

在代码中加一行打印：

```c
log_i("dma buf addr = 0x%08lX", (unsigned long)my_dma_buffer);
```

- `0x2000xxxx` → 普通 SRAM，DMA 可访问 ✅
- `0x1000xxxx` → CCM，DMA 不可访问 ❌

或者在 Keil 中打开 `.map` 文件，搜索缓冲区名称，看其所在地址。

---

## 五、附录：关键寄存器参考

### DCMI_CR 位定义（STM32F4）

| 位 | 名称 | 说明 |
|----|------|------|
| bit0 | CAPTURE | 1=使能采集 |
| bit1 | CM | 0=连续模式，1=快照模式 |
| bit4 | JPEG | 1=JPEG 格式 |
| bit6 | ESS | 0=硬件同步，1=嵌入式同步 |
| bit8 | PCKPOL | 0=上升沿采样，1=下降沿采样 |
| bit9 | HSPOL | 0=HSYNC 低有效，1=高有效 |
| bit10 | VSPOL | 0=VSYNC 低有效，1=高有效 |
| bit12-13 | FCRC | 帧捕获速率：00=所有帧 |
| bit14 | ENABLE | 1=DCMI 使能 |

### DMA2_Stream1（DCMI）配置要点

- Channel：DMA_Channel_1
- 方向：PeripheralToMemory
- 外设地址：`&DCMI->DR`（0x50050028）
- 外设数据宽度：Word（32-bit，DCMI 一次输出 4 字节）
- 内存数据宽度：Word
- 模式：Circular + DoubleBuffer
- FIFO：Enable + Full threshold（4 word）
- 中断：TC（传输完成）+ TE（传输错误，建议使能）

### OV2640 关键寄存器

| 地址 | 名称 | 说明 |
|------|------|------|
| 0xFF | 寄存器组选择 | 0x00=DSP 组，0x01=Sensor 组 |
| 0x12 | COM7 | 软复位 bit7=1，输出格式选择 |
| 0x11 | CLKRC | PCLK 分频 |
| 0x15 | COM10 | HSYNC/VSYNC/PCLK 极性反转 |
| 0x6B | DBLV | PLL 倍率、系统时钟分频、PCLK 分频 |
| 0x0D | COM8 | AGC/AWB/曝光使能 |

---

*笔记结束。这个问题从最初的"DMA 只传 4 word"到最终定位到 CCM 内存区域，前后经历了多轮排查，最大的教训是：涉及 DMA 的故障，先查缓冲区地址，再查其他。*
