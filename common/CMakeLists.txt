# 公共基础库。
#
# 依赖方向的最底层：common 不 include 任何项目内头文件，任何层都可以 include common。
# 既可作为 ESP-IDF component 编译，也可被 host 测试直接用 cc 编译（见 tests/run_host_tests.sh）。
idf_component_register(
    INCLUDE_DIRS "include"
    SRCS
        "src/cm_err.c"
        "src/cm_crc.c"
        "src/cm_tls.c"
)
