/*
 * 日志：App 唯一被允许的输出方式。
 *
 * 为什么不让 App 直接 printf：
 *   嵌入式上 printf 半数是错的（浮点不支持、阻塞在 UART、看门狗饿死），
 *   而且输出目的地是平台的事（UART / RTT / 网络）。把它固定成一个接口，
 *   平台缺失时整条链路静默降级，App 代码不用改。
 *
 * 约定：
 *   - 自动加换行，参数里不要再写 "\n"。
 *   - 单行上限 200 字节，超出截断（不要让一条日志吃掉整个栈）。
 *   - 非线程安全不需要：内部走平台 log_write，是否加锁由平台决定。
 *   - 允许在 ISR 之外任何上下文调用；ISR 里不要用。
 */
#ifndef API_LOG_H
#define API_LOG_H

#include "cm_port.h"

void app_log(const char *fmt, ...) CM_PRINTF_FMT(1, 2);

/* 带前缀的便捷版本：输出 "id: fmt..."，id 通常传 app_id(ctx)。 */
void app_logp(const char *id, const char *fmt, ...) CM_PRINTF_FMT(2, 3);

#endif /* API_LOG_H */
