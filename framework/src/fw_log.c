/*
 * api/log.h 的实现。
 *
 * 唯一职责：把可变参数格式化成一行，交给平台的 log_write。
 * 平台没有 log_write（NULL）时静默丢弃——"没接串口就看不到日志"是正常状态，
 * 不该让 App 的代码分支去处理它。
 */
#include <stdarg.h>
#include <stdio.h>

#include "app.h"
#include "fw_internal.h"
#include "plat_ops.h"

/* 单行上限。够放一条 "reader: 打开 xxx.epub 失败: IO"，又不至于吃掉任务栈。 */
#define FW_LOG_LINE_MAX 200u

static void fw_log_emit(const char *prefix, const char *fmt, va_list ap)
{
    char line[FW_LOG_LINE_MAX];
    cm_size n = 0;

    if (prefix != NULL) {
        int k = snprintf(line, sizeof(line), "%s: ", prefix);
        if (k > 0) {
            n = (cm_size)k;
        }
        if (n >= sizeof(line)) {
            n = sizeof(line) - 1u;
        }
    }

    if (n < sizeof(line)) {
        int k = vsnprintf(line + n, sizeof(line) - n, fmt, ap);
        if (k > 0) {
            n += (cm_size)k;
        }
        if (n >= sizeof(line)) {
            n = sizeof(line) - 1u;
        }
    }

    line[n] = '\0';

    const plat_ops_t *p = fw_plat();
    if (p != NULL && p->log_write != NULL) {
        (void)p->log_write(line);
    }
}

void app_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fw_log_emit(NULL, fmt, ap);
    va_end(ap);
}

void app_logp(const char *id, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fw_log_emit(id, fmt, ap);
    va_end(ap);
}
