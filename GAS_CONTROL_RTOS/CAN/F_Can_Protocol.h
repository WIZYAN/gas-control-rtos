/*
 * Version: v1.13
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明CAN协议帧结构、队列上下文和编解码接口。
 */

#ifndef F_CAN_PROTOCOL_H
#define F_CAN_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "H_Can.h"

#define F_CAN_LOCAL_TYPE               (15U) // 本机设备类型，15表示Type_FLOW流量模块。
#define F_CAN_LOCAL_ADDRESS            (1U)  // 本机CAN节点地址，即气源控制器从机地址。
#define F_CAN_CYCLE_TARGET_TYPE        (0U)  // 周期发送对象类型，0表示Type_Header表头。
#define F_CAN_CYCLE_TARGET_ADDRESS     (1U)  // 周期发送对象地址，预留为表头节点地址1。
#define F_CAN_BROADCAST_TYPE           (31U) // 目标设备类型广播值。
#define F_CAN_BROADCAST_ADDRESS        (127U)// 目标设备地址广播值。
#define F_CAN_MAX_CONSECUTIVE_VALUES   (64U) // 单次连续读请求允许的最大数据数量，覆盖当前完整地址区并限制单次占用时长。
#define F_CAN_TX_QUEUE_CAPACITY        (16U) // 待读响应帧环形队列容量。

// 自定义CAN协议功能码，与用户提供的can_module参考代码保持一致。
typedef enum
{
    F_CAN_FUNCTION_WRITE = 1,           // 定向写一个32位数据。
    F_CAN_FUNCTION_READ = 2,            // 定向读一个或多个连续32位数据。
    F_CAN_FUNCTION_BROADCAST_WRITE = 3, // 广播写，当前系统为安全起见不执行。
    F_CAN_FUNCTION_BROADCAST_READ = 4,  // 广播读一个或多个连续32位数据。
    F_CAN_FUNCTION_CYCLE = 5,           // 周期数据，当前系统不主动发送或接收。
    F_CAN_FUNCTION_WRITE_RESPONSE = 6,  // 写操作响应。
    F_CAN_FUNCTION_READ_RESPONSE = 7,   // 读操作响应。
    F_CAN_FUNCTION_HEARTBEAT = 8        // 心跳，当前系统不使用。
} F_Can_Function;

// 功能层向应用层输出的单条读写请求。
typedef struct
{
    F_Can_Function function; // 请求功能码，仅输出写、读或广播读。
    uint8_t source_type;     // 发起请求的节点类型。
    uint8_t source_address;  // 发起请求的节点地址。
    uint16_t data_address;   // 首个32位数据地址。
    uint8_t data_length;     // 连续数据数量，范围1～64。
    uint32_t value;          // 写请求携带的32位原始值，读请求时忽略。
} F_Can_Request;

// CAN自定义协议功能层上下文，不使用模块全局变量。
typedef struct
{
    H_Can_Context *hardware;                         // 所属CAN0硬件层实例。
    H_Can_Frame transmit_queue[F_CAN_TX_QUEUE_CAPACITY]; // 等待发送的响应帧队列。
    uint8_t transmit_head;                           // 响应队列下一个写入索引。
    uint8_t transmit_tail;                           // 响应队列下一个读取索引。
    F_Can_Request pending_request;                   // 等待应用层处理的请求。
    bool request_pending; // pending_request有效标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无待处理事项，true表示存在待处理事项。
    uint8_t local_type;                              // 本机节点类型。
    uint8_t local_address;                           // 本机节点地址。
    uint32_t invalid_frame_count;                    // CRC、地址或格式错误帧累计数。
    bool ready; // 协议功能层已经初始化；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未就绪，true表示已就绪。
} F_Can_Protocol_Context;

/*
 * 函数名：F_CanProtocol_Init。
 * 说明：初始化CAN0硬件层、自定义协议功能层并绑定两个上下文。
 * 输入：context为功能层上下文；hardware为CAN硬件层；local_type和local_address为本机节点标识。
 * 输出：参数、节点标识及CAN0初始化均有效时返回true，否则返回false。
 */
bool F_CanProtocol_Init(F_Can_Protocol_Context *context,
                        H_Can_Context *hardware,
                        uint8_t local_type,
                        uint8_t local_address);

/*
 * 函数名：F_CanProtocol_Deinit。
 * 说明：通过硬件层关闭CAN0并清除协议就绪状态。
 * 输入：context为CAN协议功能层上下文输入输出指针。
 * 输出：CAN0已经关闭或成功关闭时返回true，否则返回false。
 */
bool F_CanProtocol_Deinit(F_Can_Protocol_Context *context);

/*
 * 函数名：F_CanProtocol_HasFault。
 * 说明：逐层查询CAN0硬件是否存在影响通讯的故障。
 * 输入：context为只读CAN协议功能层上下文。
 * 输出：功能层未就绪或硬件层故障时返回true，否则返回false。
 */
bool F_CanProtocol_HasFault(const F_Can_Protocol_Context *context);

/*
 * 函数名：F_CanProtocol_Task。
 * 说明：非阻塞处理一帧接收数据及一帧待发送响应，并校验29位标识符和自定义CRC。
 * 输入：context 为CAN协议功能层上下文输入输出指针。
 * 输出：无；合法请求和响应发送状态保存在context中。
 */
void F_CanProtocol_Task(F_Can_Protocol_Context *context);

/*
 * 函数名：F_CanProtocol_TakeRequest。
 * 说明：取出并清除一条已经通过目标地址、长度和CRC校验的读写请求。
 * 输入：context 为功能层上下文；request 为请求输出指针。
 * 输出：成功取出请求时返回true，没有请求或参数无效时返回false。
 */
bool F_CanProtocol_TakeRequest(F_Can_Protocol_Context *context, F_Can_Request *request);

/*
 * 函数名：F_CanProtocol_QueueReadResponse。
 * 说明：将一个32位读响应加入非阻塞发送队列。
 * 输入：context 为功能层上下文；target_type和target_address为请求方节点；data_address为数据地址；value为32位原始数据。
 * 输出：响应成功入队时返回true，队列已满或参数无效时返回false。
 */
bool F_CanProtocol_QueueReadResponse(F_Can_Protocol_Context *context,
                                     uint8_t target_type,
                                     uint8_t target_address,
                                     uint16_t data_address,
                                     uint32_t value);

/*
 * 函数名：F_CanProtocol_QueueWriteResponse。
 * 说明：将写操作结果响应加入非阻塞发送队列。
 * 输入：context 为功能层上下文；target_type和target_address为请求方节点；data_address为写地址；result为协议结果码。
 * 输出：响应成功入队时返回true，队列已满或参数无效时返回false。
 */
bool F_CanProtocol_QueueWriteResponse(F_Can_Protocol_Context *context,
                                      uint8_t target_type,
                                      uint8_t target_address,
                                      uint16_t data_address,
                                      uint32_t result);

/*
 * 函数名：F_CanProtocol_CalculateCrc。
 * 说明：按参考协议对数据字节0、1、3～7及标识符功能码计算8位校验值。
 * 输入：id 为29位CAN标识符；data 为固定8字节数据区。
 * 输出：返回由Modbus CRC16中间结果右移4位得到的低8位校验值。
 */
uint8_t F_CanProtocol_CalculateCrc(uint32_t id, const uint8_t data[H_CAN_FRAME_DATA_LENGTH]);

#endif
