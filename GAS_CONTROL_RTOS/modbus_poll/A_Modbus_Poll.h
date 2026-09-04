/*
 * Version: v1.14
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明压力传感器Modbus轮询应用层上下文和任务接口。
 */

#ifndef A_MODBUS_POLL_H
#define A_MODBUS_POLL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "F_Modbus_Poll.h"
#include "gas_common.h"

// 七路压力传感器内部 Modbus 轮询应用上下文，保存地址表、轮询位置和主站事务实例。
typedef struct
{
    F_Modbus_Poll_Context master;        // 单笔非阻塞 Modbus RTU 主站事务。
    uint8_t sensor_addresses[GAS_PRESSURE_SENSOR_COUNT]; // 六瓶压力和总压力传感器的 Modbus 地址。
    uint8_t poll_index;                         // 下一路准备轮询的压力传感器索引。
    uint8_t pending_index;                      // 当前事务对应的压力传感器索引。
    uint32_t next_poll_ms;                      // 允许启动下一笔事务的时间。
    bool ready; // SCI1 主站硬件是否已经成功绑定；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未就绪，true表示已就绪。
} A_Modbus_Poll_Context;

/*
 * 函数名：A_ModbusPoll_Init。
 * 说明：初始化内部 Modbus 主站、1～7号传感器地址、六瓶压力和总压力初始质量。
 * 输入：context 为轮询应用上下文；platform 为硬件平台；system 为系统状态。
 * 输出：SCI1 主站硬件绑定成功时返回 true，否则返回 false。
 */
bool A_ModbusPoll_Init(A_Modbus_Poll_Context *context,
                       H_Gas_Platform_Context *platform,
                       Gas_System *system);

/*
 * 函数名：A_ModbusPoll_Task。
 * 说明：轮询1～7号地址的输入寄存器0和1，解析AB CD浮点压力并分别维护六瓶压力和总压力质量。
 * 输入：context 为轮询应用上下文；system 为系统状态；config 为运行参数；now_ms 为当前毫秒计数。
 * 输出：无；通过 system 更新压力、状态、通信计数和报警位。
 */
void A_ModbusPoll_Task(A_Modbus_Poll_Context *context,
                       Gas_System *system,
                       const Gas_Config *config,
                       uint32_t now_ms);

/*
 * 函数名：A_ModbusPoll_DecodePressure。
 * 说明：按照传感器AB CD字节序把两个Modbus寄存器转换为float32压力，并区分有效与超量程质量。
 * 输入：data 为四字节数据区；length 为数据长度；maximum_mpa 为运行时压力上限；pressure_mpa 和 quality 为输出指针。
 * 输出：浮点数据有限、非负且处于诊断显示范围时返回 true，并输出压力及质量；非法数据返回 false。
 */
bool A_ModbusPoll_DecodePressure(const uint8_t *data,
                                 size_t length,
                                 float maximum_mpa,
                                 float *pressure_mpa,
                                 gas_pressure_quality_t *quality);

#endif
