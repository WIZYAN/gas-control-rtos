/*
 * Version: v1.14
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明CAN0硬件层帧结构、上下文和收发接口。
 */

#ifndef H_CAN_H
#define H_CAN_H

#include <stdbool.h>
#include <stdint.h>

#define H_CAN_FRAME_DATA_LENGTH  (8U)  // 当前自定义协议固定使用8字节经典CAN数据帧。
#define H_CAN_RX_QUEUE_CAPACITY  (16U) // CAN接收中断与主循环之间的环形队列容量。
#define H_CAN_TX_MAILBOX         (0U)  // FSP配置中的扩展数据帧发送邮箱编号。
#define H_CAN_EXTENDED_ID_MAX    (0x1FFFFFFFUL) // 29位扩展CAN标识符允许的最大数值。

// 硬件层使用的通用CAN帧，不向上层暴露FSP专用数据结构。
typedef struct
{
    uint32_t id;                          // 29位扩展CAN标识符。
    uint8_t data[H_CAN_FRAME_DATA_LENGTH];// 固定8字节数据区。
} H_Can_Frame;

// CAN0硬件层上下文，保存中断接收队列和异步发送状态，不使用模块全局变量。
typedef struct
{
    H_Can_Frame receive_queue[H_CAN_RX_QUEUE_CAPACITY]; // 接收中断写入、主循环读出的环形队列。
    volatile uint8_t receive_head;       // 接收队列下一个写入位置。
    volatile uint8_t receive_tail;       // 接收队列下一个读出位置。
    volatile uint32_t dropped_frames;    // 队列满、帧格式错误或邮箱丢帧累计数量。
    volatile bool transmit_busy;         // CAN发送上下文标志；使用范围：CAN中断与协议任务之间；取值范围：false/true，false表示发送空闲，true表示CAN0正在发送一帧数据。
    volatile bool bus_off;               // CAN总线状态标志；使用范围：CAN中断与协议任务之间；取值范围：false/true，false表示总线正常，true表示CAN控制器已进入总线关闭状态。
    bool can_open; // CAN0驱动已经成功打开；使用范围：当前声明作用域内使用；取值范围：false/true，false表示关闭，true表示开启。
} H_Can_Context;

/*
 * 函数名：H_Can_Init。
 * 说明：打开FSP CAN0实例并把回调上下文绑定到当前硬件层实例。
 * 输入：context 为待初始化的CAN硬件层上下文输入输出指针。
 * 输出：CAN0成功打开并绑定回调时返回true，否则返回false。
 */
bool H_Can_Init(H_Can_Context *context);

/*
 * 函数名：H_Can_Deinit。
 * 说明：关闭当前CAN0实例并清除异步收发状态，供外部通讯模式切换使用。
 * 输入：context 为CAN硬件层上下文输入输出指针。
 * 输出：CAN0已经关闭或成功关闭时返回true，否则返回false。
 */
bool H_Can_Deinit(H_Can_Context *context);

/*
 * 函数名：H_Can_TakeFrame。
 * 说明：从接收中断环形队列取出一帧完整的29位扩展数据帧。
 * 输入：context 为CAN硬件层上下文；frame 为接收帧输出指针。
 * 输出：成功取出一帧时返回true，队列为空或参数无效时返回false。
 */
bool H_Can_TakeFrame(H_Can_Context *context, H_Can_Frame *frame);

/*
 * 函数名：H_Can_Send。
 * 说明：通过CAN0邮箱0异步发送一帧固定8字节的29位扩展数据帧。
 * 输入：context 为CAN硬件层上下文；frame 为待发送的只读帧。
 * 输出：成功启动发送时返回true，驱动忙或参数无效时返回false。
 */
bool H_Can_Send(H_Can_Context *context, const H_Can_Frame *frame);

/*
 * 函数名：H_Can_IsTransmitBusy。
 * 说明：查询CAN0是否仍在发送上一帧数据。
 * 输入：context 为只读CAN硬件层上下文指针。
 * 输出：发送尚未完成时返回true，否则返回false。
 */
bool H_Can_IsTransmitBusy(const H_Can_Context *context);

/*
 * 函数名：H_Can_HasFault。
 * 说明：查询CAN0是否处于总线关闭等不可正常通讯状态。
 * 输入：context为只读CAN硬件层上下文。
 * 输出：存在硬件通讯故障时返回true，否则返回false。
 */
bool H_Can_HasFault(const H_Can_Context *context);

#endif
