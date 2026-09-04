# AGENTS.md

## 适用范围

本文件适用于本工程及后续基于 **Renesas MCU** 的嵌入式 C 工程，包括 RA / RX / RL78 等系列。
可用于 e² studio、FSP、Smart Configurator、Code Generator，以及 GCC / LLVM / CC-RX / CC-RL 等工具链。

核心目标：

> **在保持功能、硬件行为、实时性、安全性和协议兼容性不变的前提下，以尽可能低的认知成本提高代码的可读性、可调试性和可维护性。**

代码首先是给工程师维护的，其次才是给编译器执行的。不要为了“架构漂亮”“抽象高级”或“设计模式完整”增加嵌入式项目不需要的复杂度。

---

#  设计优先级

发生冲突时，按以下顺序取舍：

1. 硬件与系统安全
2. 功能正确性
3. 实时性与确定性
4. 协议和外部接口兼容性
5. 可调试性
6. 可读性和可维护性
7. 代码复用
8. 抽象程度和代码简洁度

不得为了减少几行代码、消除少量重复或套用设计模式而牺牲前 6 项。

---

# 总体原则（硬约束）

1. 优先使用简单、直接、可搜索、可单步调试的 C 代码。
2. 不做无必要的 C++ 式面向对象模拟。
3. 不为“未来可能复用”提前创建复杂框架。
4. 重构以 **去复杂化** 为目标，而不是换一种复杂结构。
5. 修改已有工程时优先采用 **最小修改原则**。
6. 未经明确要求，不改变：
   - 对外 API
   - 通信协议格式
   - 数据存储格式
   - 中断优先级
   - RTOS 任务优先级/周期
   - GPIO 默认状态
   - 外设初始化顺序
   - 硬件控制时序
   - 安全保护逻辑
7. 对硬件行为不确定时不得猜测，必须先检查代码、FSP 配置、原理图、数据手册或项目文档。
8. 无法确认的内容必须明确标记为“未确认”，不得把推测当成事实。

---

#  C 语言基础规则

1. 新工程默认使用 **C99**；已有工程以当前编译器配置为准。
2. 优先使用固定宽度类型：

```c
uint8_t
uint16_t
uint32_t
int8_t
int16_t
int32_t
bool
```

3. 默认禁止动态内存：

```c
malloc()
calloc()
realloc()
free()
```

4. 优先静态分配、固定容量数组和模块级静态对象。
5. 禁止递归实现普通业务逻辑。
6. 不使用复杂宏隐藏业务流程。
7. 不为了“通用接口”滥用 `void *`。
8. 与寄存器、Flash 格式、通信协议相关的数据必须使用明确宽度类型。

---

# 命名规范

命名必须让维护者能通过搜索快速知道“它是什么、单位是什么、在哪里使用”。

## 变量

推荐：

```c
pressure_mpa
target_flow_ml_min
timeout_ms
elapsed_ms
bottle_index
sensor_online
rx_frame_length_bytes
```

避免：

```c
val
tmp
obj
info
data
p1
p2
```

很短的循环变量 `i`、`j` 可以使用。

##  物理量尽量带单位

推荐后缀：

```text
_ms
_us
_hz
_khz
_mv
_ma
_mpa
_percent
_ml_min
_bytes
```

不要依赖注释才能知道单位。

##  函数

推荐：

```c
PressureSensor_Init()
PressureSensor_Update()
PressureSensor_GetValue()
Valve_Open()
Valve_Close()
GasSwitch_Process()
Modbus_ParseFrame()
```

函数名必须与副作用一致：

- `Get` / `Read` 不应偷偷修改大量业务状态。
- `Check` 主要做判断，不应同时执行复杂控制。
- 修改状态使用 `Set` / `Update` / `Process` / `Start` / `Stop` 等明确动词。

##  枚举

枚举值必须带模块前缀：

```c
typedef enum
{
    GAS_STATE_IDLE = 0,
    GAS_STATE_SUPPLY,
    GAS_STATE_SWITCHING,
    GAS_STATE_FAULT
} gas_state_t;
```

---

# 常量与魔法数字

业务阈值、超时、长度、通道数、硬件参数不得散落在代码中。

推荐：

```c
#define SENSOR_TIMEOUT_MS            (3000U)
#define GAS_SWITCH_PRESSURE_MPA      (1.2f)
#define FLOW_CHANNEL_COUNT           (3U)
```

避免：

```c
if (pressure_mpa < 1.2f)
{
    ...
}
```

数字 `0`、`1` 作为明显的布尔、索引或数学常量时无需机械宏化。

---

# 数据结构设计

##  控制结构体深度

1. `struct` 嵌套原则上不超过 **2 层**。
2. 高频业务成员访问原则上不超过 **2 次成员跳转**。
3. Renesas FSP/驱动库固有结构可例外，但不得在业务层继续多层包装。

推荐：

```c
g_flow[channel].target_ml_min
```

可以接受：

```c
g_flow[channel].config.range_ml_min
```

避免：

```c
g_system.flow_manager.channels[channel].runtime.control.target_ml_min
```

禁止继续发展成：

```c
ctx->system->manager->controller->channel[index]->runtime.target
```

##  数据分类

模块内应能明确区分：

- `Config`：配置值
- `State / Runtime`：运行状态
- `Fault / Status`：故障和状态
- `Local / Temp`：函数临时量

不要把配置、实时值、错误、缓存、临时计算全部塞入一个巨大 `context`。

## 不滥用 Context

禁止为了“统一接口”把完整系统上下文层层向下传递。

如果函数只需要两个参数，就优先传两个明确参数，而不是整个 `system_context_t *`。

##  模块级状态

允许使用有明确所有权的模块级 `static` 状态：

```c
static gas_state_t s_gas_state;
static uint32_t s_state_enter_tick_ms;
static gas_fault_t s_last_fault;
```

不要因为“全局变量不好”就强行把所有状态包装成多层对象。

同时禁止无所有权、可被任意模块修改的裸全局变量。

##  指针

1. 不修改的指针参数优先加 `const`。
2. 避免多级指针。
3. 业务代码原则上避免 `void *`。
4. 不使用指针技巧隐藏数据来源。
5. 不为了“接口统一”把明确类型退化成通用指针。

##  volatile

`volatile` 仅在真正需要时使用，例如：

- MMIO 硬件寄存器
- ISR/硬件异步修改的数据
- DMA 相关共享状态

不要为了 Watch 方便把普通变量全部声明为 `volatile`。

`volatile` 不能替代原子操作、临界区、RTOS 同步和内存屏障。

---

#  模块划分

模块按 **硬件对象或业务职责** 命名。

推荐：

```text
pressure_sensor.c / .h
flow_control.c / .h
valve_control.c / .h
gas_switch.c / .h
alarm.c / .h
modbus.c / .h
storage.c / .h
```

谨慎新增：

```text
manager
service
helper
wrapper
processor
handler
context
interface
factory
```

这些名字不是绝对禁止，但必须有清晰职责并且确实降低认知成本。

##  推荐层次

通常保持：

```text
业务逻辑 / 状态机
        ↓
设备驱动模块
        ↓
Renesas FSP / HAL / 寄存器
```

避免：

```text
Service → Manager → Controller → Interface → Adapter → Driver
```

---

# Renesas 自动生成代码

使用 FSP、Smart Configurator、Code Generator 时：

1. 先识别自动生成文件和用户代码文件。
2. 默认不直接修改自动生成区域。
3. 优先通过：
   - FSP/配置界面
   - 用户代码区
   - callback
   - 官方扩展点
   - 独立业务模块
   完成功能。
4. 如果必须修改生成文件，必须说明：
   - 为什么必须修改
   - 修改的文件
   - 重新生成后是否会被覆盖
   - 如何恢复
5. 不得为了业务问题随意改写 FSP 内部驱动。
6. 未经明确要求，不修改：
   - 时钟树
   - pin 配置
   - IRQ priority
   - stack 配置
   - linker 配置

---

#  函数设计

## 单一职责

一个函数只完成一个主要任务。

推荐：

```c
void GasControl_Process(void)
{
    PressureSensor_Update();
    GasSwitch_Process();
    Purge_Process();
    Alarm_Process();
}
```

不要把采样、通信、状态判断、阀门控制、报警、日志全部写在一个几百行函数中。

##  函数长度

- 普通函数建议 **20～50 行**。
- 超过约 80～100 行时应检查是否存在多职责。
- 状态机主 `switch`、查表等可合理例外。
- 不要为了满足行数限制机械拆函数。

## 调用深度

普通业务调用链推荐不超过 **3 层**。

禁止创建只做一次转发的无意义 wrapper。

## 参数

1. 只传函数真正需要的数据。
2. 参数过多时，先检查函数是否承担了过多职责。
3. 不要为了减少参数数量而传巨大 context。
4. 输出指针必须检查空指针。

## 返回值

有失败路径、且上层需要知道结果的函数必须返回状态或错误码，例如：

- 外设读写
- 初始化
- 协议发送
- Flash/NVM
- 参数校验
- 有失败可能的控制动作

允许 `void` 的典型情况：

- 明确无失败路径的状态设置
- 周期调度入口
- 单纯通知函数

不要为了形式统一让所有函数都返回 `bool`。

## 不忽略关键返回值

底层 API 可能失败时必须决定：

- 重试
- 上报
- 记录错误
- 进入安全状态
- 或明确说明为什么可以忽略

禁止静默吞掉关键错误。

---

#  可调试性（核心要求）

现场调试应尽可能仅通过：

- Breakpoint
- Step
- Watch
- Memory
- Register
- 轻量日志

就能快速知道程序当前状态。

##  复杂条件拆成中间变量

避免：

```c
if ((ctx->status.flags & FLOW_READY_MASK) &&
    ((ctx->runtime.current / ctx->config.range) > 0.45f) &&
    Sensor_IsOnline(ctx->sensor))
{
    ...
}
```

推荐：

```c
bool flow_ready;
bool sensor_online;
float usage_ratio;

flow_ready = (status_flags & FLOW_READY_MASK) != 0U;
sensor_online = PressureSensor_IsOnline();
usage_ratio = actual_flow_ml_min / range_ml_min;

if (flow_ready && sensor_online &&
    (usage_ratio > FLOW_USAGE_MIN_RATIO))
{
    ...
}
```

这样可以直接 Watch：

```text
flow_ready
sensor_online
usage_ratio
```

##  关键状态应可观察

复杂模块根据需要保留：

```text
current_state
last_fault
last_error
state_enter_tick_ms
retry_count
communication_error_count
current_channel
```

不要为了调试堆积无用状态。

## 不隐藏重要中间结果

影响分支、控制输出或算法结果的重要中间量优先使用有意义变量，不要全部压进一条表达式。

## 调试信息不能破坏实时性

- ISR 中禁止重量级 `printf`。
- 高频任务中避免大量阻塞日志。
- 日志应可通过编译开关或等级关闭。
- Debug 代码不得改变 Release 的关键时序。

---

#  状态机

以下流程优先使用显式 `enum + switch` 状态机：

- 多步骤硬件控制
- 超时等待
- 重试
- 多阶段初始化
- 阀门/气路切换
- 通信握手
- 校准
- 故障恢复

示例：

```c
typedef enum
{
    GAS_STATE_IDLE = 0,
    GAS_STATE_SUPPLY,
    GAS_STATE_CLOSE_OLD_BOTTLE,
    GAS_STATE_OPEN_NEW_BOTTLE,
    GAS_STATE_PURGE,
    GAS_STATE_FAULT
} gas_state_t;
```

每个状态应明确：

- 进入条件
- 执行动作
- 退出条件
- 超时条件
- 故障去向
- 下一状态

状态切换不要散落在大量无关函数中。

##  避免阻塞状态机

避免：

```c
Valve_Close();
DelayMs(1000);
Valve_Open();
DelayMs(3000);
```

优先使用：

- 系统 tick
- 状态进入时间
- 非阻塞超时判断

---

#  ISR / 中断

1. ISR 必须尽可能短。
2. ISR 主要负责：
   - 读取/保存必要硬件状态
   - 清中断标志
   - 搬运少量数据
   - 设置事件标志
   - 唤醒后续处理
3. ISR 中避免：
   - 阻塞等待
   - 长循环
   - 复杂协议解析
   - 大量浮点运算
   - Flash 擦写
   - `printf`
4. ISR 与主循环/任务共享变量时检查：
   - 原子性
   - `volatile`
   - 临界区
   - 读改写竞争
5. 不得通过随意调整中断优先级掩盖真正的时序问题。

---

# 周期任务与时间处理

1. 明确周期任务的周期和目的。
2. 周期任务中禁止加入不可控时长的阻塞操作。
3. 超时优先基于 tick / timer，而不是大空循环。
4. 时间差推荐使用可处理 tick 回绕的方式：

```c
if ((uint32_t)(now_ms - start_ms) >= timeout_ms)
{
    ...
}
```

5. 修改关键流程时检查最坏执行时间是否明显增加。

---

#  FreeRTOS 附加规则

仅当工程使用 FreeRTOS 时生效：

1. 不为了“模块独立”给每个模块都建任务。
2. 新任务必须有明确的：
   - 周期/事件来源
   - 优先级依据
   - stack 大小依据
   - 阻塞方式
3. ISR 中使用对应 `FromISR` API。
4. 高优先级任务禁止长时间忙等。
5. 共享资源选择合适机制：
   - Queue
   - Semaphore
   - Mutex
   - Event Group
   - Task Notification
6. 避免无必要的大粒度锁。
7. 注意优先级反转、死锁、任务饥饿和栈溢出。
8. 未经明确要求，不修改 task priority、stack size、tick rate。

---

# 硬件驱动与业务边界

1. GPIO、ADC、UART、CAN、SPI、I2C、PWM、Timer、Flash 等底层细节应集中在对应驱动模块。
2. 业务代码尽量不散布寄存器操作。
3. 业务层表达“做什么”：

```c
Valve_Open(VALVE_MAIN);
```

而不是到处表达“哪个寄存器写哪个位”。
4. 但不要因此制造多层 HAL，通常一个清晰设备驱动层即可。
5. 硬件特殊限制必须注释“为什么”。

---

# 通信协议

适用于 UART / RS485 / Modbus / CAN / SPI 协议 / 自定义协议。

1. 接收缓冲区必须检查长度边界。
2. 解析字段前确认帧长度足够。
3. 明确处理大小端。
4. 不推荐将原始 `uint8_t *` 直接强转为协议结构体指针。
5. 不依赖编译器结构体 padding 作为线协议格式。
6. CRC、帧头、长度、命令字逻辑保持清晰。
7. 非法帧不得污染上一帧有效状态。
8. 通信超时、CRC 错误、长度错误建议独立计数，便于调试。
9. 协议重构不得静默改变已有字段语义。
10. 标准协议优先遵循标准行为。

---

# 错误处理与安全状态

1. 错误码应具有模块语义。
2. 不用模糊 `-1` 表示所有错误，除非接口极其简单。
3. 关键模块建议区分：
   - 参数错误
   - 通信错误
   - 超时
   - 设备离线
   - 数据越界
   - 硬件执行失败
4. 故障发生后应明确：
   - 是否重试
   - 最大重试次数
   - 是否自动恢复
   - 是否进入安全状态
   - 是否需要人工清除
5. 安全相关输出优先采用明确 fail-safe 策略。
6. 不允许为了“继续运行”静默吞掉关键错误。

---

#  注释

注释主要解释：

- 为什么这样设计
- 为什么需要这个时序/延时
- 为什么边界值不能随意改
- 协议为什么这样处理
- 某 workaround 对应什么硬件问题

不要写只翻译代码的注释。

差：

```c
/* 判断压力是否低 */
if (pressure_mpa < GAS_SWITCH_PRESSURE_MPA)
```

好：

```c
/*
 * 工作瓶压力低于切换阈值后认为无法继续稳定供气，
 * 此时进入备用瓶切换流程。
 */
if (pressure_mpa < GAS_SWITCH_PRESSURE_MPA)
```

公共 API 和复杂关键函数推荐使用简洁 Doxygen 注释。

不要为了形式给每个简单 `static` 函数生成大段模板注释。

---

#  头文件

模块 `.h` 应让维护者快速看懂：

- 模块职责
- 核心数据类型
- 状态/错误码
- 对外 API

内部实现细节放在 `.c`。

禁止为了方便把大量内部变量通过 `extern` 暴露出去。

---

#  修改已有代码前的强制流程

任何非纯注释修改前，必须先：

1. 阅读目标 `.c` / `.h`。
2. 找到模块入口。
3. 查找参数定义。
4. 查找初始化位置。
5. 查找所有写入点。
6. 查找所有读取点。
7. 查找最终使用位置。
8. 梳理调用链：

```text
caller → target function → child calls
```

9. 检查是否涉及：
   - ISR
   - DMA
   - Timer
   - Watchdog
   - RTOS task
   - callback
   - FSP 自动生成配置
   - Flash/NVM
   - 通信协议
   - 安全保护
10. 明确最小修改集合。
11. 明确回退方式。

对于关键参数必须能够回答：

```text
在哪里定义？
在哪里初始化？
谁修改？
谁读取？
最终在哪里影响硬件或业务行为？
```

---

#  Codex / AI 修改规则

## 修改前

1. 先阅读相关代码，不根据文件名猜实现。
2. 涉及已有参数、结构体、状态机时先搜索完整数据流。
3. 优先判断能否局部修改解决。
4. 不清楚硬件行为时先查现有实现和配置。

## 修改中

1. 优先最小修改。
2. 不做与当前任务无关的“顺手重构”。
3. 不擅自批量重命名文件、函数、类型、公共 API。
4. 不擅自升级 FSP、编译器或第三方库。
5. 不擅自修改时钟、pin、IRQ priority、RTOS priority。
6. 能用简单 C 解决，不新增复杂设计模式。
7. 能用一个清晰函数解决，不创建多层对象体系。
8. 能用浅层结构体解决，不创建多层 context。
9. 复杂判断拆出可观察的中间变量。
10. 新增关键状态时考虑断点和 Watch 是否方便。
11. 修改硬件控制逻辑时优先保持原访问顺序与时序。

## 修改后必须说明

1. 修改了哪些文件。
2. 每个文件修改了什么。
3. 为什么这样修改。
4. 主流程从哪里进入。
5. 关键参数在哪里定义、初始化和修改。
6. 关键状态变量有哪些。
7. 建议在哪里设置断点。
8. 建议 Watch 哪些变量。
9. 哪些硬件/协议行为确认保持不变。
10. 哪些内容因缺少硬件或环境未实际验证。

---

#  新模块生成规则

新增模块前先判断是否真的需要新模块。

确实需要时优先：

```text
module.c
module.h
```

避免一次生成：

```text
module_manager.c
module_service.c
module_interface.c
module_factory.c
module_context.c
```

新模块应满足：

- 入口清晰
- 状态清晰
- API 数量适中
- 数据所有权清晰
- 无隐藏副作用
- 易于断点和 Watch 调试

---

#  推荐重构方向

优先允许：

- 降低结构体嵌套
- 删除无意义 wrapper
- 拆解巨大函数中的独立职责
- 复杂条件拆中间变量
- 用 enum 替代不清晰状态数字
- 消除魔法数字
- 改善变量和函数名称
- 清理稳定且明显的重复逻辑
- 补充必要错误处理
- 增加关键调试状态

以下场景禁止一次性大改：

- 已稳定运行的硬件控制
- 安全相关逻辑
- 时序敏感模块
- 缺少测试的旧模块
- 当前没有硬件验证环境

此时优先小步修改。

---

# 编译与静态检查

每次修改后尽可能检查：

- 工程编译通过
- 不新增 warning
- signed / unsigned 问题
- 类型截断
- 数组越界
- NULL 指针
- 整数溢出
- 单位转换
- 未初始化变量
- 返回值处理

如果当前环境无法实际编译 Renesas 工程，必须明确说明 **“未实际编译”**，不得声称已经验证通过。

---

#  硬件回归检查

涉及硬件行为的修改至少考虑：

- 上电默认状态
- 初始化顺序
- GPIO 默认输出
- 外设启动顺序
- 中断
- DMA
- 通信
- 超时
- 传感器异常
- 设备断线
- 边界值
- Watchdog
- 复位恢复

安全相关控制还必须确认故障时是否进入预期安全状态。

---

# 协议回归检查

涉及 Modbus / CAN / UART / 自定义协议时至少检查：

- 帧长度
- 地址
- 命令字
- 大小端
- CRC/checksum
- 超时
- 异常码
- 重试
- 最大长度
- 非法帧
- 半包/粘包（如适用）

内部重构不得改变外部设备看到的协议行为。

---

#  禁止项

除非任务明确要求，否则禁止：

1. 为了“更现代”重写稳定模块。
2. 为了“更优雅”增加层层抽象。
3. 未确认情况下改变硬件时序。
4. 随意修改 FSP 自动生成代码。
5. 引入动态内存。
6. 使用递归实现普通业务逻辑。
7. 使用复杂宏隐藏核心流程。
8. 无原因使用函数指针表或虚表式设计。
9. 无原因使用 generic `void *` 接口。
10. 吞掉关键底层错误。
11. 用固定延时掩盖真正的状态同步问题。
12. ISR 中执行重量级逻辑。
13. 为减少代码行数编写难以单步调试的表达式。
14. 把所有变量做成全局变量。
15. 把所有变量塞进一个巨大 context。
16. 只为通过编译而改变业务行为。

---

#  推荐代码风格示例

推荐：

```c
static void GasSwitch_ProcessSupply(void)
{
    float pressure_mpa;
    bool pressure_low;
    bool backup_available;

    pressure_mpa = PressureSensor_GetCachedValue();
    pressure_low = (pressure_mpa <= GAS_SWITCH_PRESSURE_MPA);
    backup_available = Bottle_HasBackup();

    if (!pressure_low)
    {
        return;
    }

    if (!backup_available)
    {
        GasFault_Set(GAS_FAULT_NO_BACKUP);
        return;
    }

    GasState_Enter(GAS_STATE_CLOSE_OLD_BOTTLE);
}
```

避免：

```c
if ((ctx->gas.manager.channel[ctx->gas.current].sensor.value <=
     ctx->config.gas.switch_cfg.threshold) &&
    BottleManager_GetService(ctx)->CheckNext(ctx))
{
    GasController_GetInstance(ctx)->SwitchService->Start(ctx);
}
```

后者的问题：

- 参数来源难找
- 结构体访问过深
- 调用链过深
- 中间判断无法方便 Watch
- 对象职责不清
- 人工维护成本高

---

#  最终交付清单

完成任务前检查：

- [ ] 原有硬件行为是否保持
- [ ] 协议兼容性是否保持
- [ ] 是否避免新增无意义抽象层
- [ ] `struct` 是否保持浅层
- [ ] 是否避免过长成员访问链
- [ ] 是否避免巨大 context 透传
- [ ] 函数职责是否清晰
- [ ] 复杂条件是否方便 Watch
- [ ] 关键状态是否能快速定位
- [ ] 是否消除新增魔法数字
- [ ] 物理量单位是否清晰
- [ ] 关键返回值是否处理
- [ ] ISR 是否保持短小
- [ ] 是否避免新增阻塞流程
- [ ] Renesas 自动生成代码是否得到保护
- [ ] 是否实际编译；未编译是否明确说明
- [ ] 是否给出建议断点
- [ ] 是否给出建议 Watch 变量
- [ ] 是否说明未验证部分

---

#  最终判断标准

当存在多个可行方案时，优先选择能让另一名嵌入式工程师做到以下事情的方案：

> **打开工程后，不依赖原作者，也不依赖 AI，仅通过代码、断点和 Watch，就能较快回答：程序现在处于什么状态、关键参数从哪里来、为什么进入这个分支、下一步会控制哪个硬件。**

如果一个“更高级”的设计让这些问题更难回答，就不应采用该设计。
