/* App 注册表。见 app_manifest.h 里的"加一个 App 要做什么"。 */
#include "app_manifest.h"

extern const app_desc_t g_app_manager;
extern const app_desc_t g_app_hello;

/* 顺序有意义：SYSTEM App 由 fw_boot() 直接拉起，其余由 0 号按此顺序启动。
 * 0 号放第一个，因为它要负责启动别人。 */
static const app_desc_t *const s_apps[] = {
    &g_app_manager,
    &g_app_hello,
};

const app_desc_t *const *app_manifest(uint32_t *count)
{
    if (count != NULL) {
        *count = (uint32_t)CM_ARRAY_SIZE(s_apps);
    }
    return s_apps;
}
