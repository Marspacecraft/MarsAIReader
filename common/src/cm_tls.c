/*
 * cm_tls 的默认实现：C11 `_Thread_local` 数组。
 *
 * 标了 CM_WEAK：平台包如果发现自己的编译器没有可用 TLS 运行时，可以按同样签名
 * 提供一份实现覆盖它，而不需要改这个文件，也不需要给 common 加平台分支。
 * 为什么不用函数指针表：那要引入初始化顺序（谁在什么时候绑定后端），
 * 而 common 的约定是没有 cm_init()。
 */
#include "cm_tls.h"

#include "cm_port.h"

static _Thread_local void *s_slots[CM_TLS_SLOT_MAX];

CM_WEAK void cm_tls_set(uint32_t slot, void *value)
{
    if (slot >= CM_TLS_SLOT_MAX) {
        return;
    }
    s_slots[slot] = value;
}

CM_WEAK void *cm_tls_get(uint32_t slot)
{
    if (slot >= CM_TLS_SLOT_MAX) {
        return NULL;
    }
    return s_slots[slot];
}
