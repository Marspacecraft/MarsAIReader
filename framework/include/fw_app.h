/*
 * 框架层控制面（只给 apps/_system/ 与测试用）。
 *
 * 与 api/app.h 的区别：
 *   api/app.h      —— App 对自己能做的一切（查自己的状态、延时、退出）
 *   framework/...  —— 对**别人**能做的一切（启动它、停止它、看它活着没）
 *
 * 后者能停掉别的任务、能观察全局，属于"特权"。所以它不在 api/ 里，
 * 普通 App 的 include 白名单里没有它。
 */
#ifndef FW_APP_H
#define FW_APP_H

#include "cm_types.h"
#include "cm_err.h"
#include "app.h"

typedef struct fw_app fw_app_t;

/* 注册表容量。不做动态分配：启动阶段的 malloc 失败很难优雅处理，
 * 而这个数字改一次就够用很久。 */
#define FW_APP_MAX 16u

/* 平台自检 + 注册 App 清单。硬必需项缺失时返回 CM_ERR_NOT_SUPPORTED，
 * 并在日志里指出是哪一项。
 *
 * 不把 plat_ops_t 作为参数：那是平台内部类型，不该出现在控制面头里。
 * 平台包通过 plat_get_ops() 提供自己，框架只认这一个入口。 */
cm_err_t fw_init(const app_desc_t *const *manifest, uint32_t count);

/* fw_init() + 启动所有 APP_FLAG_SYSTEM 的 App（0 号）。
 * 返回后系统已经在跑——0 号会去启动其余的 App。 */
cm_err_t fw_boot(const app_desc_t *const *manifest, uint32_t count);

uint32_t fw_app_count(void);
fw_app_t *fw_app_at(uint32_t index);
fw_app_t *fw_app_find(const char *id);

const app_desc_t *fw_app_desc(const fw_app_t *a);
app_state_t fw_app_state_of(const fw_app_t *a);

/* 启动。只允许从 EMPTY / STOPPED 出发，其他状态返回 CM_ERR_STATE。 */
cm_err_t fw_app_start(fw_app_t *a);

/* 请求停止：置取消位，状态转 STOPPING。
 * 协作式——真正停下要等 App 走到下一个阻塞点，所以停止后请轮询状态。 */
void fw_app_request_stop(fw_app_t *a);

#endif /* FW_APP_H */
