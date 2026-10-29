/*
 * 极简测试运行器（header-only，只给 tests/sys/ 用）。
 *
 * 为什么不用现成框架：这里只需要"断言 + 选择跑哪个用例 + 退出码"，几十行就够。
 * 引一个框架进来，反而要在 Windows / Linux / 交叉工具链上各配一次。
 *
 * 用法：
 *
 *   static void list_push_back(void)
 *   {
 *       TT_CHECK(...);
 *   }
 *
 *   static const tt_case_t s_cases[] = {
 *       TT_CASE(list_push_back),
 *       TT_CASE(list_remove),
 *   };
 *
 *   TT_MAIN(s_cases)
 *
 * 命令行：
 *   无参数      跑全部用例
 *   用例名      只跑该用例
 *   --list      列出用例名（一行一个）
 *
 * 退出码：0 通过 / 1 有用例失败 / 2 用例名不存在
 */
#ifndef TINYTEST_H
#define TINYTEST_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef void (*tt_fn_t)(void);

typedef struct {
    const char *name;
    tt_fn_t fn;
} tt_case_t;

/* 每个翻译单元各自一份计数器：测试文件之间不共享状态 */
static int tt_checks;
static int tt_failures;

static inline void tt_fail(const char *file, int line, const char *what)
{
    printf("  FAIL %s:%d  %s\n", file, line, what);
    tt_failures++;
}

#define TT_CHECK(cond)                                                         \
    do {                                                                       \
        tt_checks++;                                                           \
        if (!(cond)) {                                                         \
            tt_fail(__FILE__, __LINE__, #cond);                                \
        }                                                                      \
    } while (0)

#define TT_CHECK_EQ_U32(actual, expected)                                      \
    do {                                                                       \
        tt_checks++;                                                           \
        unsigned long a_ = (unsigned long)(actual);                            \
        unsigned long e_ = (unsigned long)(expected);                          \
        if (a_ != e_) {                                                        \
            printf("  FAIL %s:%d  %s == 0x%lX, expected 0x%lX\n",              \
                   __FILE__, __LINE__, #actual, a_, e_);                       \
            tt_failures++;                                                     \
        }                                                                      \
    } while (0)

#define TT_CHECK_STR(actual, expected)                                         \
    do {                                                                       \
        tt_checks++;                                                           \
        const char *a_ = (actual);                                             \
        const char *e_ = (expected);                                           \
        if (a_ == NULL || e_ == NULL || strcmp(a_, e_) != 0) {                 \
            printf("  FAIL %s:%d  %s == \"%s\", expected \"%s\"\n",            \
                   __FILE__, __LINE__, #actual, a_ ? a_ : "(null)",            \
                   e_ ? e_ : "(null)");                                        \
            tt_failures++;                                                     \
        }                                                                      \
    } while (0)

static inline int tt_run(const tt_case_t *cases, size_t n, int argc, char **argv)
{
    if (argc > 1 && (strcmp(argv[1], "--list") == 0 || strcmp(argv[1], "-l") == 0)) {
        for (size_t i = 0; i < n; i++) {
            printf("%s\n", cases[i].name);
        }
        return 0;
    }

    if (argc > 1) {
        for (size_t i = 0; i < n; i++) {
            if (strcmp(argv[1], cases[i].name) == 0) {
                cases[i].fn();
                if (tt_failures) {
                    printf("FAIL: %s (%d checks, %d failures)\n", cases[i].name,
                           tt_checks, tt_failures);
                    return 1;
                }
                printf("PASS: %s (%d checks)\n", cases[i].name, tt_checks);
                return 0;
            }
        }
        printf("unknown case: %s\navailable:", argv[1]);
        for (size_t i = 0; i < n; i++) {
            printf(" %s", cases[i].name);
        }
        printf("\n");
        return 2;
    }

    for (size_t i = 0; i < n; i++) {
        const int before = tt_failures;
        cases[i].fn();
        if (tt_failures != before) {
            printf("  -> case %s failed\n", cases[i].name);
        }
    }

    if (tt_failures) {
        printf("FAIL: %d failures in %d checks\n", tt_failures, tt_checks);
        return 1;
    }
    printf("PASS: %zu cases, %d checks\n", n, tt_checks);
    return 0;
}

#define TT_CASE(fn) { #fn, fn }

#define TT_MAIN(cases)                                                         \
    int main(int argc, char **argv)                                            \
    {                                                                          \
        return tt_run((cases), sizeof(cases) / sizeof((cases)[0]), argc, argv); \
    }

#endif /* TINYTEST_H */
