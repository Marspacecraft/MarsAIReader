/*
 * App 清单：整个固件里"有哪些 App"的唯一注册点。
 *
 * 加一个 App 的成本（也是给 AI 的规则）：
 *   1. 在 apps/<id>/ 下实现 init / exit / task 三个函数；
 *   2. 定义 const app_desc_t g_app_<id>；
 *   3. 在 apps/app_manifest.c 里加一行 extern + 数组里加一项。
 *
 * 三处都在 apps/ 里，不需要改 framework/ 或 platform/。这就是"AI 只能改应用层"
 * 那条约束在代码上的落点：如果加一个 App 必须动框架，说明框架漏了能力。
 */
#ifndef APP_MANIFEST_H
#define APP_MANIFEST_H

#include "cm_types.h"
#include "app.h"

/* 返回只读数组与元素个数。指针在进程生命周期内有效（静态常量）。
 * count 可以为 NULL。 */
const app_desc_t *const *app_manifest(uint32_t *count);

#endif /* APP_MANIFEST_H */
