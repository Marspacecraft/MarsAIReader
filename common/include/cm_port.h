/*
 * 标准 C 兼容层。
 *
 * 存在的理由：上层（common / api / lib / framework / apps）只写标准 C11，
 * 不写 "#ifdef 某个芯片/某个编译器"。凡是"标准里应该有、但工具链之间名字或
 * 可用性不一致"的东西，都在这里收敛成一组宏。上层看到的名字永远不变。
 *
 * 这里只放**编译期**能力，不放运行期抽象（那是 platform/port 的事）。
 *
 * 支撑的平台：GCC / Clang 系列工具链（目标 SDK 与 host 编译器都属于这类）。
 * 不假装支持 MSVC：cm_atomic 依赖 __atomic_* 内建，假装支持等于埋一个测不到的坑。
 */
#ifndef CM_PORT_H
#define CM_PORT_H

/* ---------------- 1. 标准版本闸门 ---------------- */

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 201112L)
#error "common 需要 C11：请用 -std=c11 编译"
#endif

#if !defined(__GNUC__) && !defined(__clang__)
#error "common 需要 GCC 或 Clang：__atomic_* 与 __attribute__ 是硬依赖"
#endif

/* ---------------- 2. 语言关键字 ---------------- */

/* 头文件里的函数一律 static inline，不产生外部符号，也不占一个翻译单元 */
#define CM_INLINE   static inline

#define CM_RESTRICT restrict
#define CM_ALIGNOF  _Alignof
#define CM_ALIGNAS  _Alignas

/* 这里刻意不用标准 C11 的 _Noreturn 关键字：
 *   _Noreturn void f(void);   // 只能放在声明前面
 *   void f(void) _Noreturn;   // 语法错误
 * 而函数指针与后缀写法都很常见，用属性版两者都成立。 */
#define CM_NORETURN __attribute__((noreturn))

/* ---------------- 3. 编译器属性 ---------------- */

/* 分支预测提示。未命中时几乎免费，命中时能省掉一次跳转 */
#define CM_LIKELY(x)   __builtin_expect(!!(x), 1)
#define CM_UNLIKELY(x) __builtin_expect(!!(x), 0)

/* 编译期断言：失败信息里带 msg，比运行期 assert 早得多 */
#define CM_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

#define CM_PACKED       __attribute__((packed))
#define CM_WEAK         __attribute__((weak))
#define CM_ATTR_UNUSED  __attribute__((unused))
#define CM_DEPRECATED   __attribute__((deprecated))
#define CM_PRINTF_FMT(a, b) __attribute__((format(printf, a, b)))

/* 编译器屏障：只阻止编译器重排，不做 CPU 级栅栏。
 * 需要 CPU 级顺序时用 cm_atomic_* 的 release/acquire。 */
#define CM_COMPILER_BARRIER() __asm__ volatile("" ::: "memory")

/* ---------------- 4. 内建函数探测 ---------------- */

#define CM_HAS_BUILTIN(x) __has_builtin(x)

#endif /* CM_PORT_H */
