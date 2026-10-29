/*
 * 统一错误码。
 *
 * 为什么全项目只有一个错误枚举：
 *   - 分层的错误码（plat_err_t / fw_err_t / disp_err_t）在每次跨层调用时都要转换，
 *     MCU 上这些转换代码是纯开销，且转换函数本身会写错（默认值吞掉错误）。
 *   - 错误语义天然是全局的：NOT_SUPPORTED 在平台层、框架层、应用层含义一致。
 *   - 因此各层可以保留自己的类型名（plat_err_t 等）作为 cm_err_t 的别名，但值同源。
 *
 * 值一旦发布不得改动（会进日志、进 IPC、进持久化）。
 */
#ifndef CM_ERR_H
#define CM_ERR_H

#include <stdbool.h>

typedef enum {
    CM_OK = 0,

    /* ---- 调用方错误：重试同样的参数不会成功 ---- */
    CM_ERR_INVALID      = 1,   /* 参数非法：空指针、越界、未初始化句柄 */
    CM_ERR_STATE        = 2,   /* 当前状态不允许该操作（如未 open 就 read） */
    CM_ERR_DENIED       = 3,   /* 无权限 / 能力未授予 */

    /* ---- 环境错误：换个时间可能成功 ---- */
    CM_ERR_NOMEM        = 4,   /* 内存不足（含配额耗尽） */
    CM_ERR_BUSY         = 5,   /* 资源被占用，非阻塞获取失败 */
    CM_ERR_TIMEOUT      = 6,   /* 等待超时 */
    CM_ERR_IO           = 7,   /* 总线 / 器件 / 文件系统错误 */
    CM_ERR_NOENT        = 8,   /* 目标不存在 */

    /* ---- 能力错误：这个是"这台机器做不到"，不是"这次失败" ---- */
    CM_ERR_NOT_SUPPORTED = 9,  /* 能力缺失：平台没实现，或该板无此器件 */
    CM_ERR_UNAVAILABLE   = 10, /* 有能力但当前不可用：服务未启动 / 已 FAILED */

    /* ---- 控制流 ---- */
    CM_ERR_CANCELLED    = 11,  /* 被取消（App 停止、请求撤销） */
    CM_ERR_AGAIN        = 12,  /* 暂无数据，稍后再试（非错误） */

    CM_ERR_MAX          = 13
} cm_err_t;

/* 语义分组。CM_ERR_NOT_SUPPORTED / CM_ERR_UNAVAILABLE 必须分开：
 *   NOT_SUPPORTED → UI 应隐藏入口，重试无意义
 *   UNAVAILABLE   → UI 应提示"稍后重试" */
static inline bool cm_err_is_capability(cm_err_t e)
{
    return e == CM_ERR_NOT_SUPPORTED || e == CM_ERR_UNAVAILABLE;
}

static inline bool cm_err_is_retryable(cm_err_t e)
{
    return e == CM_ERR_BUSY || e == CM_ERR_TIMEOUT ||
           e == CM_ERR_AGAIN || e == CM_ERR_UNAVAILABLE;
}

/* 返回静态字符串，永不返回 NULL。用于日志，不用于 UI 文案。 */
const char *cm_err_str(cm_err_t e);

#endif /* CM_ERR_H */
