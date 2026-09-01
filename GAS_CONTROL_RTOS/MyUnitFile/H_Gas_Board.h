/*
 * Version: v1.13
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明RA4M1板级初始化回调接口。
 */

#ifndef H_GAS_BOARD_H
#define H_GAS_BOARD_H

#include "bsp_api.h"

/*
 * 函数名：R_BSP_WarmStart。
 * 说明：处理 FSP 启动阶段事件并应用板级引脚配置。
 * 输入：event 为当前 BSP 启动阶段。
 * 输出：无。
 */
void R_BSP_WarmStart(bsp_warm_start_event_t event);

#endif
