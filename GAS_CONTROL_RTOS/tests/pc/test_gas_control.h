/*
 * Version: v1.12
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明PC回归测试使用的气源控制测试接口。
 */

#ifndef TEST_GAS_CONTROL_H
#define TEST_GAS_CONTROL_H

#include "A_Gas_Control.h"
#include "F_Hmi.h"

/*
 * 函数名：main。
 * 说明：执行三阀七状态、外部通讯、串口屏、日志物理清除、分类分页和自动切瓶测试。
 * 输入：无。
 * 输出：全部测试通过时返回 0。
 */
int main(void);

#endif
