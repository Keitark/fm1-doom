#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

int __wrap_vsnprintf(char *, size_t, const char *, va_list);
int __wrap_snprintf(char *, size_t, const char *, ...);
int __wrap_vsprintf(char *, const char *, va_list);
int __wrap_sprintf(char *, const char *, ...);
int fm1_doom_sdk_print(char **, char *, const char *, va_list);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void compare(const char *format, ...)
{
    char actual[1024], expected[1024];
    int want, got;
    size_t size;
    va_list input, args;
    va_start(input, format);
    va_copy(args, input); want = vsnprintf(expected, sizeof(expected), format, args); va_end(args);
    CHECK(want >= 0 && want < (int)sizeof(expected));
    for (size = 0; size <= (size_t)want + 2u; ++size) {
        memset(actual, 0x5a, sizeof(actual));
        va_copy(args, input); got = __wrap_vsnprintf(size ? actual + 1 : NULL, size, format, args); va_end(args);
        CHECK(got == want); CHECK(actual[0] == 0x5a); CHECK(actual[size + 1] == 0x5a);
        if (size) {
            size_t written = (size_t)want < size ? (size_t)want : size - 1u;
            CHECK(!memcmp(actual + 1, expected, written)); CHECK(actual[written + 1] == 0);
        }
    }
    va_copy(args, input); got = __wrap_vsprintf(actual, format, args); va_end(args);
    CHECK(got == want && !strcmp(actual, expected));
    va_end(input);
}

static int sdk_print(char **cursor, char *last, const char *format, ...)
{
    int result; va_list args; va_start(args, format);
    result = fm1_doom_sdk_print(cursor, last, format, args); va_end(args); return result;
}

int main(void)
{
    char text[128];
    int i, n = -1;
    compare("plain %% %c %s", 'Q', "test");
    compare("%d %i %u %x %X %o", INT_MIN, INT_MAX, UINT_MAX, UINT_MAX, 0xabcdefu, UINT_MAX);
    compare("%ld %lu %lld %llu", LONG_MIN, ULONG_MAX, LLONG_MIN, ULLONG_MAX);
    compare("%hhd %hhu %hd %hu", -129, 511u, -32769, 131071u);
    compare("%jd %ju %zd %zu %td %tx", INTMAX_MIN, UINTMAX_MAX, (ptrdiff_t)-12, (size_t)15, (ptrdiff_t)-7, (uintptr_t)31);
    compare("[%+08d][% 08d][%-8.5d][%08.5u]", -72, 13, 24, 5u);
    compare("[%#x][%#X][%#o][%#.0o][%.0x][%#.0x][%+.0d]", 0xabcu, 0xabcu, 9u, 0u, 0u, 0u, 0);
    compare("[%*.*d][%*.*s][%8c][%-8s]", -12, 8, -731, 10, 3, "abcdef", 'Z', "abc");
    compare("[%.*s][%.*d][%.2d][%.2u]", 0, "abcdef", -3, 12, 8, 0u);
    compare("%llu:%llo:%llx", ULLONG_MAX, ULLONG_MAX, ULLONG_MAX);
    for (i = -80; i <= 80; ++i) {
        uint64_t value = ((uint64_t)(uint32_t)(i * 379 + 0x1234567) << 32) | (uint32_t)(i * 983 + 0xabcdef);
        compare("%+0*.*d:%#0*.*x:%#llo:%llu", i % 15, i % 10, i, i % 17, i % 9, (unsigned)i, (unsigned long long)value, (unsigned long long)value);
    }
    CHECK(__wrap_snprintf(text, sizeof(text), "%p:%p", (void *)(uintptr_t)0xabc, (void *)0) == 9);
    CHECK(!strcmp(text, "0xabc:0x0"));
    CHECK(__wrap_snprintf(text, sizeof(text), "%f/%d/%s/%Lf/%u", 1.25, -12, "aligned", (long double)2.5, 33u) == 30);
    CHECK(!strcmp(text, "<float>/-12/aligned/<float>/33"));
    CHECK(__wrap_sprintf(text, "%s%n/%d", "abc", &n, 7) == 5 && n == 3 && !strcmp(text, "abc/7"));
    CHECK(__wrap_snprintf(NULL, 0, "%*s", INT_MAX, "") == INT_MAX);
    CHECK(__wrap_snprintf(text, 1, "%*sX", INT_MAX, "") == -1 && text[0] == 0);
    CHECK(__wrap_snprintf(text, 1, "%*s%*sAB", INT_MAX, "", INT_MAX, "") == -1 && text[0] == 0);
    {
        char *cursor = text;
        CHECK(sdk_print(&cursor, text + 3, "%d-%s", 12345, "tail") == 3);
        CHECK(cursor == text + 3 && !strcmp(text, "123"));
        CHECK(sdk_print(&cursor, NULL, "%s", "456") == 3);
        CHECK(cursor == text + 6 && !strcmp(text, "123456"));
        CHECK(sdk_print(NULL, NULL, "%d", 7) == 0);
    }
    puts("integer formatter contract passed");
    return 0;
}
