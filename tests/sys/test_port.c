/*
 * cm_port.h（标准 C 兼容层）的 host 测试。
 *
 * 目的不是测"宏有没有定义"，而是测**这组宏能否让上层只写标准 C**：
 *   - 前置与后缀两种 CM_NORETURN 写法都能编译；
 *   - CM_INLINE 不产生外部符号也能内联；
 *   - CM_STATIC_ASSERT 在文件作用域可用；
 *   - CM_ALIGNOF / CM_RESTRICT / CM_HAS_BUILTIN 在 host 编译器上行为正确。
 *
 * 运行全部：      python tools/run_tests.py
 * 只跑本文件：    python tools/run_tests.py test_port
 * 只跑一个用例：  python tools/run_tests.py test_port inline_restrict
 * 列出用例：      python tools/run_tests.py test_port --list
 */
#include <stdio.h>
#include <string.h>

#include "cm_port.h"
#include "cm_types.h"

#include "tinytest.h"

/* 后缀写法：标准 C11 的 _Noreturn 关键字在这里会语法错误，
 * 所以 cm_port 用的是属性版。这条测试就是防止有人把它改回关键字。 */
static void fatal(const char *msg) CM_NORETURN;
static void fatal(const char *msg)
{
    printf("FATAL: %s\n", msg);
    for (;;) {
        /* 不返回 */
    }
}

CM_INLINE int cm_test_add(int a, int b)
{
    return a + b;
}

/* 把指针交给编译器，验证 CM_RESTRICT 可用 */
CM_INLINE cm_size cm_test_strlen(const char *CM_RESTRICT s)
{
    return (cm_size)strlen(s);
}

CM_STATIC_ASSERT(sizeof(cm_u32) == 4u, "cm_u32 必须是 4 字节");
CM_STATIC_ASSERT(sizeof(cm_i64) == 8u, "cm_i64 必须是 8 字节");
CM_STATIC_ASSERT(CM_ALIGNOF(cm_u8) == 1u, "cm_u8 对齐必须是 1");

/* 后缀 CM_NORETURN 的声明/定义能编译，本身就是被验证的东西；
 * 这里再取一次地址，让编译器和人都不至于把它当死代码删掉。 */
static void noreturn_attr(void)
{
    void (*volatile fp)(const char *) = &fatal;
    TT_CHECK(fp != NULL);
}

static void inline_restrict(void)
{
    /* CM_INLINE：无外部符号、可正常调用 */
    TT_CHECK(cm_test_add(2, 3) == 5);
    TT_CHECK(cm_test_add(-1, 1) == 0);

    /* CM_RESTRICT 只影响优化，行为必须与普通指针一致 */
    TT_CHECK(cm_test_strlen("abc") == 3);
}

static void likely_unlikely(void)
{
    /* 编译器内建探测：GCC/Clang 都该为真，否则 CM_LIKELY 的语义不成立 */
    TT_CHECK(CM_HAS_BUILTIN(__builtin_expect) != 0);

    /* CM_LIKELY / CM_UNLIKELY 不得改变取值 */
    TT_CHECK(CM_LIKELY(1) == 1);
    TT_CHECK(CM_UNLIKELY(0) == 0);
}

static void common_macros(void)
{
    /* CM_BIT / CM_ARRAY_SIZE 等通用宏在 cm_types.h 里，顺带确认与 cm_port 共存 */
    TT_CHECK(CM_BIT(3) == 8);
    {
        int arr[4];
        TT_CHECK(CM_ARRAY_SIZE(arr) == 4);
    }
}

static const tt_case_t s_cases[] = {
    TT_CASE(noreturn_attr),
    TT_CASE(inline_restrict),
    TT_CASE(likely_unlikely),
    TT_CASE(common_macros),
};

TT_MAIN(s_cases)
