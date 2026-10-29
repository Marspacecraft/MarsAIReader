/*
 * 应用层的唯一入口头。
 *
 * 规矩：AI 写的 App **只**能 include 这个文件（加上 cm_*.h 与 lib/ 里的头）。
 * 想加能力时不要伸手去 framework/ 或 platform/，而是先问"api 是不是漏了"——
 * 这个头是闭合的：App 需要的一切都应该在这里找得到。
 *
 * 现在提供的能力：
 *   app.h —— App 形态、生命周期、伪阻塞延时、内存
 *   log.h —— 唯一的输出方式
 *
 * 还没提供（按优先级）：disp / input / audio / stor / net / power / res / cap。
 * 每加一个，同步加进这里 + 能力目录 + skill。
 */
#ifndef API_H
#define API_H

#include "cm_types.h"
#include "cm_err.h"

#include "app.h"
#include "log.h"

#endif /* API_H */
