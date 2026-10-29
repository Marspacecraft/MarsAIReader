#include "cm_err.h"

#include "cm_types.h"

/* 下标必须与 cm_err_t 的值一一对应。顺序改动会静默错位，
 * 所以用编译期断言把"表长度 == 枚举长度"钉死。 */
static const char *const s_err_str[CM_ERR_MAX] = {
    [CM_OK]                = "OK",
    [CM_ERR_INVALID]       = "INVALID",
    [CM_ERR_STATE]         = "STATE",
    [CM_ERR_DENIED]        = "DENIED",
    [CM_ERR_NOMEM]         = "NOMEM",
    [CM_ERR_BUSY]          = "BUSY",
    [CM_ERR_TIMEOUT]       = "TIMEOUT",
    [CM_ERR_IO]            = "IO",
    [CM_ERR_NOENT]         = "NOENT",
    [CM_ERR_NOT_SUPPORTED] = "NOT_SUPPORTED",
    [CM_ERR_UNAVAILABLE]   = "UNAVAILABLE",
    [CM_ERR_CANCELLED]     = "CANCELLED",
    [CM_ERR_AGAIN]         = "AGAIN",
};

CM_STATIC_ASSERT(CM_ARRAY_SIZE(s_err_str) == (cm_size)CM_ERR_MAX,
                 "cm_err_t 与错误码字符串表不一致");

const char *cm_err_str(cm_err_t e)
{
    if ((unsigned)e >= (unsigned)CM_ERR_MAX) {
        return "UNKNOWN";
    }
    const char *s = s_err_str[e];
    return (s != NULL) ? s : "UNKNOWN";
}
