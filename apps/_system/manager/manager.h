/*
 * 0 号 App：管理器。
 *
 * 它做的事情必须"只有它能做"——即：需要跨 App 视野的事。
 *   - 启动 1 号、2 号……（fw_boot 只拉起 SYSTEM App，其余归它）
 *   - 观测状态变化（FAILED 要有人看见，否则就是静默死亡）
 *   - 将来：配额、看门狗、按功耗策略停 App
 *
 * 它不该做的事（这些是框架的机制）：
 *   - 创建任务、管锁、管队列 —— 那是 framework/
 *   - 碰硬件 —— 那是服务型 App（display/input/audio）与平台层
 */
#ifndef MANAGER_H
#define MANAGER_H

#include "cm_types.h"
#include "app.h"

extern const app_desc_t g_app_manager;

#endif /* MANAGER_H */
