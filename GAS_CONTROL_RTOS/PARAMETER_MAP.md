# 参数映射表

> 适用版本：V1.14（2026-09-04）<br>
> 维护规则：关键参数的定义、初始化、修改、读取或最终用途发生变化时，必须同步更新本表。<br>
> 范围：只记录跨周期保存、影响硬件或业务结果的关键数据；函数内部临时变量不在本表重复列出。

# 数据分类与所有权

| 分类 | 当前所有者 | 说明 |
| --- | --- | --- |
| 配置 `Config` | `A_Gas_Control_Context::config` | 当前真正生效的13项运行参数；EEPROM保存成功前，HMI、CAN和Modbus只修改各自候选副本。 |
| 运行状态 `State / Runtime` | `A_Gas_Control_Context::system`及同级状态机字段 | 压力、气瓶状态、阀门命令镜像、切瓶步骤和截止时间。 |
| 故障状态 `Fault / Status` | `Gas_System::alarm_bits`、`platform_ready`、`mode` | 报警可以并存；安全门槛不能只用一个报警位代替。 |
| 硬件状态 | `H_Gas_Platform_Context` | 最近一次成功GPIO写入的镜像、12 V吸合状态、`valve_io_error`和SCI1中断/错误标志。 |
| 协议候选数据 | HMI、CAN、外部Modbus各自上下文 | 只负责接收、解析和暂存；业务层验证并持久化成功后才替换生效配置。HMI固定FIFO和丢弃计数也归协议层所有。 |

主配置数据流：

```text
编译期默认值 / AT24C256有效记录
        -> A_Gas_Control_Context::config（当前生效）
HMI / CAN / 外部Modbus输入
        -> 各模块候选副本
        -> 统一范围和关系校验
        -> A_GasConfig_Save()写入并读回确认
        -> context->config = candidate（原子生效）
        -> 状态机、阀门层、压力轮询读取
        -> GPIO、状态、报警、显示和日志结果
```

# 运行参数 `Gas_Config`

所有字段都定义在 `MyUnitFile/gas_common.h::Gas_Config`，生效实例定义在
`MyUnitFile/A_Gas_Control.h::A_Gas_Control_Context::config`。

上电时，`MyUnitFile/A_Gas_Control.c::A_GasControl_Init()`先调用
`A_GasConfig_LoadDefaults()`；AT24C256可用时，`A_GasConfig_Load()`只有在记录标识、版本、CRC、
单项范围和参数关系全部有效后，才用存储值替换默认值。HMI可编辑11个画面字段，并自动补齐
`switch_release_mpa`和`pressure_fresh_ms`两个隐藏字段；CAN和外部Modbus只开放前10项，
不会修改后三项HMI安全参数。

| 参数 | 分类/单位 | 初始化/恢复 | 候选修改与最终生效 | 直接读取位置 | 最终用途 |
| --- | --- | --- | --- | --- | --- |
| `switch_pressure_mpa` | Config / MPa | `A_GasConfig_LoadDefaults()`；`A_GasConfig_DecodeCommonPayload()` | HMI字段0、`A_Can_AssignParameterCandidate()`、`A_Modbus_ReadConfigRegisters()`；最终由`A_GasControl_ProcessHmiConfig()`或`A_GasControl_ProcessExternalConfig()`保存成功后赋给`context->config` | `A_GasControl_SwitchTask()`、`A_GasConfig_Validate()` | 工作瓶压力不高于此值时进入并维持低压确认。 |
| `switch_release_mpa` | Config / MPa | 默认值；EEPROM公共载荷 | HMI不单独输入，由`A_HmiConfig_ApplyHiddenParameters()`生成为切瓶压力+0.100 MPa；CAN/Modbus可直接写；统一保存后生效 | `A_GasControl_SwitchTask()`、`A_GasConfig_Validate()` | 低压确认期间压力恢复到此值或以上时取消本次切换。 |
| `valve_pull_in_time_ms` | Config / ms | 默认200 ms；EEPROM公共载荷 | HMI字段4、CAN、Modbus；统一保存后生效 | `F_ValveControl_SetSupply()`、`F_ValveControl_SetExhaust()`、`F_ValveControl_SetTest()`、`F_ValveControl_SetTotalTest()` | 传给`H_GasPlatform_Write*Valve()`，确定开阀后的12 V强吸合时长。 |
| `ready_min_pressure_mpa` | Config / MPa | 默认1.500 MPa；EEPROM公共载荷 | HMI字段1、CAN、Modbus；统一保存后生效 | `A_GasControl_UpdateCylinderStates()`、`A_GasControl_FindNextReady()`、`A_GasConfig_Validate()` | 决定非工作瓶能否进入待测试/待用并参与备瓶选择。 |
| `pressure_max_mpa` | Config / MPa | 默认25.000 MPa；EEPROM公共载荷 | HMI字段3、CAN、Modbus；统一保存后生效 | `A_ModbusPoll_Task()`、`A_GasControl_ReclassifyPressureQuality()`、`A_GasConfig_Validate()` | 判定新样本或已有样本是有效还是超量程，间接影响控制资格、显示和上报。 |
| `low_confirm_time_ms` | Config / ms | 默认1000 ms；EEPROM公共载荷 | HMI字段5、CAN、Modbus；统一保存后生效 | `A_GasControl_SwitchTask()` | 与独立低压样本数共同确认持续低压，过滤瞬时波动。 |
| `low_confirm_samples` | Config / 次 | 默认3次；EEPROM公共载荷 | HMI字段6、CAN、Modbus；统一保存后生效 | `A_GasControl_SwitchTask()` | 规定低压确认必须累计的新压力样本数。 |
| `valve_close_wait_ms` | Config / ms | 默认500 ms；EEPROM公共载荷 | HMI字段7、CAN、Modbus；统一保存后生效 | `A_GasControl_SwitchTask()` | 旧供气阀关闭后，在`GAS_SWITCH_DEAD_TIME`等待机械释放。 |
| `valve_open_wait_ms` | Config / ms | 默认500 ms；EEPROM公共载荷 | HMI字段8、CAN、Modbus；统一保存后生效 | `A_GasControl_SwitchTask()` | 新供气阀打开后，在`GAS_SWITCH_VERIFY_NEW`等待稳定再提交新工作瓶。 |
| `pressure_fresh_ms` | Config / ms | 默认2500 ms；EEPROM公共载荷 | HMI不单独输入，`A_HmiConfig_ApplyHiddenParameters()`固定为默认值；CAN/Modbus可直接写；统一保存后生效 | `A_ModbusPoll_Task()`、`A_GasControl_PressureIsFresh()` | 到期把压力标为陈旧，并禁止陈旧样本参与状态判断和自动换瓶。 |
| `low_warning_pressure_mpa` | Config / MPa | 默认2.000 MPa；V3/V4载荷恢复，V2沿用默认 | 仅HMI字段2可改；CAN/Modbus更新其他参数时保留当前值；HMI保存成功后生效 | `A_GasControl_UpdateCylinderStates()`、`A_GasControl_SwitchTask()`、`A_GasConfig_Validate()` | 在使用与低压警告两种工作瓶状态之间分类。 |
| `manual_exhaust_time_ms` | Config / ms | 默认5000 ms；V3/V4载荷恢复，V2沿用默认 | 仅HMI字段9可改，画面以秒和三位小数输入；HMI保存成功后生效 | `A_GasControl_StartExhaust()` | 排气阀实际开启成功后生成`exhaust_deadline_ms`。 |
| `test_valve_max_time_ms` | Config / ms | 默认600000 ms（10分钟）；V4按分钟恢复，V3旧数值迁移为同数值分钟，V2沿用默认 | 仅HMI字段10可改，范围5～60整分钟；HMI保存成功后生效 | `A_GasControl_TotalTestTask()` | 分路测试阀实际开启成功后生成`test_deadline_ms`，不是从按钮请求时刻开始计时。 |

# 系统、气瓶与安全状态

这些字段定义在 `MyUnitFile/gas_common.h`。`A_GasControl_Init()`先把完整控制上下文清零，再显式建立业务初态；
阀门命令字段只有在对应硬件写入成功后才更新，因此可作为“最近一次成功执行命令”的软件镜像。

| 参数 | 初始化位置 | 修改位置 | 读取位置 | 最终用途 |
| --- | --- | --- | --- | --- |
| `Gas_Cylinder::pressure_mpa`、`pressure_quality` | `A_GasControl_Init()`清零；`A_ModbusPoll_Init()`设质量为`INVALID` | `A_ModbusPoll_Task()`成功解析或判定陈旧；`A_ModbusPoll_RecordFailure()`处理连续失败；参数生效后`A_GasControl_ReclassifyPressureQuality()`重判量程 | `A_GasControl_UpdateCylinderStates()`、`A_GasControl_FindNextReady()`、`A_GasControl_SwitchTask()`、HMI/CAN/Modbus/日志 | 压力到七状态再到备瓶资格和自动换瓶的主数据源。 |
| `Gas_Cylinder::pressure_timestamp_ms` | 完整上下文清零为0 | `A_ModbusPoll_Task()`仅在成功解析新帧时写`now_ms` | `A_ModbusPoll_Task()`、`A_GasControl_PressureIsFresh()`、恢复与低压样本逻辑 | 判定新鲜度，并保证5 ms任务不会重复累计同一压力样本。 |
| `Gas_Cylinder::comm_fail_count` | `A_ModbusPoll_Init()`清零 | `A_ModbusPoll_RecordFailure()`累加；成功帧清零 | 压力轮询失败处理 | 达到3次产生通信警告，达到10次使样本失效并影响控制资格。 |
| `Gas_Cylinder::state` | `A_GasControl_Init()`设`GAS_CYL_INIT` | `A_GasControl_UpdateCylinderStates()`、`A_GasControl_SwitchTask()`、`A_GasControl_SetCylinderDisabled()`、测试结论入口 | 选瓶、阀门人工操作门槛、安全检查、HMI/CAN/Modbus/事件日志 | 表示初始化、待用、使用、低压待换、低压警告、停用或待测试。 |
| `Gas_Cylinder::qualification_passed` | `A_GasControl_Init()`设`false` | `A_GasControl_SetQualificationPassed()`；进入低压待换或停用时清除 | `A_GasControl_UpdateCylinderStates()`、`A_GasControl_FindNextReady()`、显示/协议/日志 | 与压力和阀门条件共同决定能否进入待用并被自动选瓶。 |
| `recovery_sample_count`、`recovery_sample_timestamp_ms` | 完整上下文清零 | `F_GasControl_EnterLowReplace()`、`F_GasControl_RecoveryPressureConfirmed()` | `A_GasControl_UpdateCylinderStates()` | 低压瓶恢复后只累计3个独立合格样本，再进入待测试。 |
| `supply_cmd` | 上下文清零；`F_ValveControl_Init()`在硬件全关成功后确认清零 | `F_ValveControl_SetSupply()`仅在GPIO写成功后更新；`F_ValveControl_AllOff()`仅在25路输出全关成功后清零 | 阀门互锁、状态更新、选瓶、安全不变量、HMI/CAN/Modbus/日志 | 保证最多一路供气，并表示最近一次成功执行的供气阀命令。 |
| `exhaust_cmd` | 同上 | `F_ValveControl_SetExhaust()`及成功的`F_ValveControl_AllOff()` | 超时、互锁、停用、安全检查及HMI/CAN/Modbus/日志 | 表示排气阀已接受的命令状态，并驱动自动到时关闭。 |
| `test_cmd` | 同上 | `F_ValveControl_SetTest()`及成功的`F_ValveControl_AllOff()` | 总测试阀联动、超时、互锁、安全检查及HMI/CAN/Modbus/日志 | 表示分路测试阀已接受的命令状态。 |
| `exhaust_deadline_ms` | 上下文清零；成功全关再次清零 | `A_GasControl_StartExhaust()`开阀成功后设置；单阀关闭成功或全关成功后才清零，GPIO关阀失败时保留原到期值 | `A_GasControl_ManualValveTask()` | 到时调用`F_ValveControl_SetExhaust(..., false)`；失败时后续5 ms周期仍可重试，不会误认为已关阀。 |
| `test_deadline_ms` | 上下文清零；成功全关再次清零 | 建立待开请求时清零；`A_GasControl_TotalTestTask()`在分路实际开启后设置；关闭/停用/全关时清零 | `A_GasControl_ManualValveTask()` | 从实际分路开阀时刻起计时，到时关闭分路并在最后一路结束后关闭总阀。 |
| `Gas_System::total_pressure` | 上下文清零；`A_ModbusPoll_Init()`设地址7、质量`INVALID` | `A_ModbusPoll_Task()`更新压力、质量、时间戳和失败计数 | HMI、CAN、Modbus、常规日志 | 只用于显示、记录与通信诊断，不参与六瓶状态和自动换瓶。 |
| `Gas_System::date_time` | 上下文清零且`valid=false` | `HMI/A_Hmi.c::A_Hmi_Task()`仅在解析后SCI9仍正常时写入合法RTC响应；超过5秒未更新或`A_GasControl_HandleHmiFault()`发现SCI9故障时置无效 | `A_GasLog_Task()`、HMI日志查询流程 | 决定是否允许新增日志，并提供日志时间；通信错误批次中的RTC不会生效。 |
| `Gas_System::total_test_cmd` | 上下文清零；成功全关确认`false` | 仅`F_ValveControl_SetTotalTest()`在总阀GPIO写成功后更新；成功全关时清零 | 分路开阀门槛、总测试状态机、安全不变量、全阀关闭检查 | `VAL_CAL`总测试阀命令镜像；CAN/Modbus没有独立控制地址。 |
| `Gas_System::mode` | `A_GasControl_Init()`设`GAS_MODE_AUTO` | 开新瓶失败的`A_GasControl_SwitchTask()`或`A_GasControl_CheckOutputInvariant()`设`STOPPED` | 初选、切瓶、人工开阀门槛及协议/日志上报 | 严重故障后停止自动开阀；当前没有运行时恢复AUTO入口。 |
| `Gas_System::switch_state` | 上下文清零得到`GAS_SWITCH_IDLE` | `A_GasControl_SwitchTask()`推进；停用、安全故障或尚未进入机械动作时的参数生效路径可复位到`IDLE` | 状态更新、机械切换忙判断、CAN/Modbus/日志 | 决定非阻塞自动换瓶下一次5 ms周期要执行的步骤。 |
| `Gas_System::active_index` | `A_GasControl_Init()`设`GAS_NO_ACTIVE_CYLINDER` | 初始选瓶、换瓶验证完成、停用当前瓶、开阀失败和安全不变量故障路径 | 状态分类、切瓶、工作瓶传感器诊断、协议和日志 | 唯一工作瓶索引；0～5对应1～6号，`0xFF`表示当前无工作瓶。 |
| `Gas_System::alarm_bits` | 上下文清零；初始化按各模块结果置位 | 压力、阀门、存储、HMI、CAN/Modbus及业务安全路径分别置/清自己的位 | CAN控制详细结果、CAN/Modbus只读状态、事件日志 | 可并存的诊断位图，不替代`mode`或`platform_ready`的安全判断。 |
| `Gas_System::platform_ready` | `A_GasControl_Init()`写入`H_GasPlatform_Init()`结果 | 阀门/传感器初始化失败、`F_ValveControl_Task()`的12 V转5 V失败、业务开关阀GPIO错误或紧急全关失败时置`false`；当前无运行时恢复为`true`路径 | 初选、切瓶、排气/测试开阀、总测试推进及CAN详细错误 | `false`时拒绝所有开阀动作，关阀仍允许；即使随后全关成功，运行期硬件错误造成的平台锁定也不自动解除。 |

# 气源总控过程状态

以下字段定义在 `MyUnitFile/A_Gas_Control.h::A_Gas_Control_Context`，只由单一`GasControl`任务拥有。

| 参数 | 初始化/修改位置 | 读取位置 | 最终用途 |
| --- | --- | --- | --- |
| `switch_old_index`、`switch_new_index` | 初始化为`0xFF`；`A_GasControl_SwitchTask()`选择、提交或清除；停用相关瓶时复位 | `A_GasControl_UpdateCylinderStates()`、`A_GasControl_SwitchTask()` | 保存机械切换期间的旧瓶与候选新瓶，避免提前改变正式工作瓶身份。 |
| `switch_low_sample_count` | 上下文清零；`A_GasControl_SwitchTask()`累计；取消低压或参数生效重启确认时清零 | `A_GasControl_SwitchTask()` | 与持续时间双条件确认低压。 |
| `switch_low_last_sample_ms` | 上下文清零；每次计入新样本时更新；参数生效重启确认时清零 | `A_GasControl_SwitchTask()` | 防止同一压力样本被5 ms循环重复计数。 |
| `switch_low_start_ms` | 上下文清零；首次进入低压确认时写`now_ms`；重启确认时清零 | `A_GasControl_SwitchTask()` | 与`low_confirm_time_ms`组成低压持续时间判断。 |
| `switch_enter_ms` | 初始化时写当前平台毫秒；进入`DEAD_TIME`和`VERIFY_NEW`时更新 | `A_GasControl_SwitchTask()` | 分别实现旧阀关闭等待和新阀开启验证等待。 |
| `total_test_pending_open_mask` | 上下文清零；`A_GasControl_SetTestValve()`置位/清位；实际开启或失败时由`A_GasControl_TotalTestTask()`清位 | 测试阀入口、总测试状态机、安全不变量 | bit0～bit5保存1～6号分路测试阀的待开请求。 |
| `test_open_not_before_ms[6]` | 上下文清零；测试请求、总阀成功开启和取消请求时更新 | `A_GasControl_TotalTestTask()` | 保证总测试阀先开，并等待共享12 V吸合间隔后才开对应分路。 |
| `emergency_close_pending` | 上下文清零；阀门初始化全关失败、12 V转5 V失败、阀门GPIO错误或`A_GasControl_ForceAllValvesOff()`开始时置`true`，仅在全部GPIO关闭成功后清`false` | `A_GasControl_Task()`、`A_GasControl_TotalTestTask()` | 失败后立即结束本周期，跳过同步EEPROM等非关键业务并在下个5 ms周期优先重试；成功前禁止新的测试阀推进且不清软件/硬件镜像。 |
| `external_comm_mode` | 先设默认CAN，再由`A_GasControl_LoadCommMode()`用有效EEPROM记录覆盖 | `A_GasControl_SetExternalCommMode()`在维护门槛、目标接口初始化和保存成功后切换；失败时逐项检查目标关闭、原记录回写和原接口恢复，并保留对应报警；主周期及外部请求分发函数读取 | 保证同一时刻只运行CAN或SCI0/Modbus之一，并掉电保存选择；回滚失败不再被静默吞掉。 |

# 硬件与轮询时序状态

| 参数 | 定义/初始化 | 修改与读取 | 最终用途 |
| --- | --- | --- | --- |
| `H_Gas_Platform_Context::supply_state[]`、`exhaust_state[]`、`test_state[]`、`total_test_state` | `MyUnitFile/H_Gas_Platform.h`；`H_GasPlatform_Init()`清零 | 对应GPIO成功写入后更新；`H_GasPlatform_AllValvesOff()`失败时保留，全部成功后清零 | 记录最近一次成功硬件写入状态，避免关阀失败后软件误报全关。 |
| `boost_state[6]`、`boost_deadline_ms[6]` | 同上，初始化为0/false | `H_GasPlatform_WriteValveOutput()`成功开启时设置；`H_GasPlatform_ValveTask()`到期关闭`VAL_Px`并清除 | 控制12 V强吸合到约5 V保持的非阻塞切换；失败时后续周期重试。 |
| `boost_interval_active[6]`、`boost_available_ms[6]` | 同上，初始化为0/false | 每次成功启动强吸合后设置500 ms门槛；写阀、阀门任务和总测试可开判断读取 | 限制同组相邻强吸合脉冲；运行中全关不会清除此门槛，防止绕过保护。 |
| `H_Gas_Platform_Context::valve_io_error` | `H_GasPlatform_Init()`随硬件上下文清零 | 任一阀门或`VAL_Px` GPIO写失败时锁存`true`；只有`H_GasPlatform_AllValvesOff()`全25路关闭全部成功后清除；每条HMI按钮、人工关闭、状态更新、自动换瓶、总测试及外部控制后均检查 | 置位`GAS_ALARM_PLATFORM_NOT_READY`、令`platform_ready=false`并触发紧急全关，使当前批次后续开阀在已知故障后不能继续执行。 |
| `H_Gas_Platform_Context::sensor_uart_error`、`F_Modbus_Poll_Context::result` | 平台/轮询上下文初始化清零 | SCI1方向、发送、接收或回调失败时锁存；`F_ModbusPoll_FinishTransaction()`在中止前读取原错误，并检查中止/恢复接收方向结果 | 任一底层失败都生成`MODBUS_POLL_RESULT_IO`，与`TIMEOUT/CRC/PROTOCOL`区分后进入传感器失败计数。 |
| `A_Modbus_Poll_Context::poll_index`、`pending_index` | `modbus_poll/A_Modbus_Poll.h`；`A_ModbusPoll_Init()`清零并建立地址表 | `A_ModbusPoll_Task()`在启动事务时保存`pending_index`，取结果后推进`poll_index` | 轮转1～7号传感器，并把返回帧写回正确气瓶或总压力。 |
| `A_Modbus_Poll_Context::next_poll_ms` | 初始化为0 | 每笔成功或失败事务取完结果后写`now_ms + 100 ms`；主站空闲且到时才发下一地址 | 表示事务之间的最小启动间隔，不代表100 ms内同时处理全部7个地址。 |
| `A_Modbus_Poll_Context::ready` | `A_ModbusPoll_Init()`写入`F_ModbusPoll_Init()`结果 | `A_ModbusPoll_Task()`入口读取 | 未绑定SCI1时停止轮询并触发平台/传感器报警路径。 |
| `H_Modbus_Context::uart_error` | `H_Modbus_Init()`清零 | SCI0奇偶、帧、溢出或BREAK组合错误、方向GPIO、匹配电阻GPIO、发送启动、发送后恢复接收方向或反初始化失败时置`true`；`H_Modbus_HasFault()`读取 | 让`A_Modbus_HasFault()`置位`GAS_ALARM_EXTERNAL_MODBUS`，并为通讯模式回滚提供失败依据。 |
| `F_At24c256_Context::last_result` | `F_At24c256_Init()`清零后由每次存储操作更新 | `H_SoftIic_WriteByte()`分别返回总线执行结果和ACK；F层据此写入`BUS`或`NACK`，写完成轮询耗尽写`TIMEOUT`，START/STOP/读字节/WP失败写`BUS`，成功写`OK`；页写和自检保存并恢复原失败码 | 防止真实GPIO/时钟故障被当作器件忙；BUS时先做一次有界总线恢复，再由上层以失败结果设置存储报警。 |
| `F_At24c256_Context::write_protect_fault` | `F_At24c256_Init()`清零 | 任一次WP引脚配置或电平写入失败后锁存为`true`，仅重新初始化清除 | 在事务首个错误码需要保留时，仍单独留下写保护恢复失败的诊断证据。 |

AT24C256的参数、日志和通讯模式记录最终都通过`A_Storage → F_At24c256 → H_SoftIic`访问。
`F_At24c256_SelfTest()`在备份成功并进入测试写阶段后，无论测试写、读回或比较结果如何都尝试恢复原两个字节，并再次读回确认；恢复失败或恢复数据不一致优先报告恢复错误。
这些读写及写完成轮询仍为同步调用；日志查询和物理清除的“每5 ms周期最多一次存储操作”只限制操作数量，不限制单次访问时长。
100 kHz下64字节页写的总线时序本身就可能超过5 ms。这是V1.14未改变的实时性技术债，应在实机测量WCET，若超出周期预算再评估独立低优先级存储任务或可中断的分步状态机。

# HMI事件和诊断数据流

| 参数 | 定义/初始化 | 修改与读取 | 最终用途 |
| --- | --- | --- | --- |
| `button_queue[8]`、`button_queue_head`、`button_queue_count` | `HMI/F_Hmi.h::F_Hmi_Context`；`F_Hmi_Init()`清零 | `F_Hmi_ParseFrame()`仅将严格14字节的B1 11按键和合法B1 14菜单按到达顺序入队；`A_GasControl_Task()`逐条处理并在每条后检查阀门及SCI9错误 | 保留连续操作，同时禁止超长残帧或首条GPIO失败后的后续事件形成开阀。 |
| `text_queue[8]`、`text_queue_head`、`text_queue_count` | 同上 | `F_Hmi_ParseFrame()`将合法B1 11文本入队；`A_GasControl_ProcessHmiTextInputs()`循环调用日志和参数消费者，两者都不识别时调用`F_Hmi_DiscardTextEvent()` | 保证未知画面/控件文本不会长期占据队首并阻塞后续日志或参数输入。 |
| `button_event_drop_count`、`text_event_drop_count` | 同上，初始化为0 | 对应FIFO已满时累加；主动丢弃未知文本时也累加文本计数；达`UINT32_MAX`后饱和 | 仅作现场Watch诊断，不改变现有协议帧或界面显示。 |
| `H_Hmi_Context::uart_error` | `H_Hmi_Init()`清零 | SCI9奇偶、帧、溢出或BREAK组合错误以及发送错误时锁存；`H_Hmi_HasFault()`在解析前后、文本阶段后、每条按钮后及队列返回后读取 | 置位`GAS_ALARM_HMI_COMM`，立即使`date_time.valid=false`并丢弃硬件环形缓冲、协议残帧、RTC待取值和待执行输入；自动控制继续，故障后的HMI控制输入保持禁用。 |

# 日志查询条件数据流

| 参数 | 定义/初始化 | 修改位置 | 快照与读取 | 最终用途 |
| --- | --- | --- | --- | --- |
| `A_Hmi_Log_Context::edit_filter` | `HMI/A_Hmi_Log.h`；`A_HmiLog_Init()`调用`A_HmiLog_SetDefaultFilter()`建立全部时间/气瓶/状态 | `A_HmiLog_InputTask()`依FIFO顺序提交日期时间；`A_HmiLog_HandleButton()`处理全部时间、下拉菜单和重置 | 查询前允许继续编辑，不直接用于正在执行的扫描 | 保存Screen6当前输入条件。 |
| `A_Hmi_Log_Context::active_filter` | 初始化时复制默认条件 | `A_HmiLog_Request()`在处理查询按钮时一次性执行`active_filter = edit_filter` | `A_HmiLog_FilterValid()`及`A_HmiLog_RecordMatchesFilter()`只读取该快照 | 保证查询期间后续输入不会改变当前结果；时间范围使用包含开始和结束的闭区间。 |
| `A_Hmi_Log_Filter::time_enabled`及起止年月日时分秒 | 默认`false`、2000-01-01 00:00:00至2099-12-31 23:59:59 | 任一合法日期/时间输入后置`true`；全部时间按钮或重置条件可关闭 | 先比较完整日期时间键，再扫描EEPROM记录 | 决定查询全部时间或限定闭区间，避免仅比较日字段导致跨日误匹配。 |
| `cylinder_number`、`target_state` | 默认0表示全部 | Screen6下拉菜单索引写入1～6号瓶或1～7状态 | `A_HmiLog_RecordMatchesFilter()`仅对事件记录应用 | 事件日志按气瓶及“变化后的新状态”组合筛选；常规日志忽略这两项。 |

# 建议断点与 Watch

| 目的 | 建议断点 | 建议Watch |
| --- | --- | --- |
| 上电初始化与故障门槛 | `A_GasControl_Init()`末尾、`A_GasControl_ForceAllValvesOff()` | `context->system.platform_ready`、`context->emergency_close_pending`、`context->system.alarm_bits` |
| 压力帧归属和新鲜度 | `A_ModbusPoll_Task()`取得`result_pending`后 | `poll_index`、`pending_index`、`next_poll_ms`、目标`pressure_mpa/pressure_quality/pressure_timestamp_ms` |
| 自动换瓶 | `A_GasControl_SwitchTask()` | `switch_state`、`active_index`、`switch_old_index`、`switch_new_index`、`switch_low_sample_count`、`switch_enter_ms` |
| 排气阀 | `A_GasControl_StartExhaust()`、`A_GasControl_ManualValveTask()` | 对应`exhaust_cmd`、`exhaust_deadline_ms`、`platform_ready` |
| 测试阀与总阀 | `A_GasControl_SetTestValve()`、`A_GasControl_TotalTestTask()` | `total_test_pending_open_mask`、`test_open_not_before_ms[index]`、`total_test_cmd`、`test_cmd`、`test_deadline_ms` |
| 阀门GPIO失效安全 | `F_ValveControl_Task()`、`A_GasControl_ForceAllValvesOff()` | `valve_io_error`、`platform_ready`、`emergency_close_pending`、`boost_state[]`、三类阀门镜像 |
| 内部Modbus IO失败 | `F_ModbusPoll_StartRead()`、`F_ModbusPoll_FinishTransaction()` | `sensor_uart_error`、`state`、`result`、`result_pending`、`comm_fail_count` |
| HMI爆发/未知输入及通信故障 | `F_Hmi_QueueButtonEvent()`、`A_GasControl_ProcessHmiTextInputs()`、`A_GasControl_HandleHmiFault()` | `button_queue_count`、`text_queue_count`、`button_event_drop_count`、`text_event_drop_count`、`hmi.hardware.uart_error`、`system.date_time.valid` |
| 外部Modbus与模式回滚 | `H_Modbus_Init()`、`H_Modbus_Send()`、`A_GasControl_SetExternalCommMode()` | `uart_open`、`uart_error`、`external_comm_mode`、`target_closed`、`previous_restored`、`previous_mode_saved`、通讯/存储报警位 |
| 参数保存与生效 | `A_GasControl_ProcessHmiConfig()`、`A_GasControl_ProcessExternalConfig()` | `candidate`、`context->config`、参数结果码、`GAS_ALARM_STORAGE` |
| 日志时间筛选 | `A_HmiLog_InputTask()`、`A_HmiLog_Request()`、`A_HmiLog_RecordMatchesFilter()` | `edit_filter`、`active_filter`、`filter_error`、`scanned_count`、`matched_count` |

# V1.14文档变更说明

| 项目 | 说明 |
| --- | --- |
| 变更范围 | 在保持正常控制时序的前提下，补全内部Modbus、阀门GPIO/保持切换、HMI事件、EEPROM软件IIC、外部Modbus及通讯模式切换的失败检测、回滚和诊断数据流；阀门故障在同批后续开阀前进入失效安全，全关失败周期立即返回；SCI9错误批次的RTC和控制输入一并失效；移除无读取者的`scheduler_started`。 |
| 影响模块 | `modbus_poll`、`H_Gas_Platform`、`F_Valve_Control`、`A_Gas_Control`、`HMI/F_Hmi`与`H_Hmi`、`AT24C256`、`modbus/H_Modbus`、`A_Gas_Rtos`及配套文档。FSP/RTOS配置、大彩V1.12画面、CAN/Modbus字节协议、EEPROM参数/日志格式与正常阀门时序未改。 |
| 验证方式 | 对每个符号核对定义、初始化、写入、读取和失败去向；PC严格警告回归及`-fanalyzer`覆盖预接收顺序、阀门失效安全、HMI FIFO/未知文本和帧长边界，Arm GCC 10.3.1全量链接通过。实机GPIO/SCI/EEPROM故障注入及同步存储路径WCET仍待板卡验证。 |
