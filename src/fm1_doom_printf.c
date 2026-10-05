/* Target string formatting. Strong definitions prevent the SDK floating-point
 * formatter archive member from loading before LTO. Host tests use distinct
 * names so these entry points can be compared to the real CRT. */
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#ifndef FM1_DOOM_PRINTF_HOST_TEST
#define __wrap_vsnprintf vsnprintf
#define __wrap_snprintf snprintf
#define __wrap_vsprintf vsprintf
#define __wrap_sprintf sprintf
#define fm1_doom_sdk_print print
#endif

typedef struct {
    char *data;
    size_t size;
    size_t count;
} output_t;

static void repeat(output_t *out, char value, size_t count)
{
    size_t writable = out->size && out->count < out->size - 1u
                    ? out->size - 1u - out->count : 0u;
    size_t i;
    if (writable > count) writable = count;
    for (i = 0; i < writable; ++i) out->data[out->count + i] = value;
    out->count = (size_t)-1 - out->count < count ? (size_t)-1 : out->count + count;
}

static void string(output_t *out, const char *value, size_t count)
{
    size_t writable = out->size && out->count < out->size - 1u
                    ? out->size - 1u - out->count : 0u;
    size_t i;
    if (writable > count) writable = count;
    for (i = 0; i < writable; ++i) out->data[out->count + i] = value[i];
    out->count = (size_t)-1 - out->count < count ? (size_t)-1 : out->count + count;
}

static int decimal(const char **format)
{
    int value = 0;
    while (**format >= '0' && **format <= '9') {
        int digit = *(*format)++ - '0';
        value = value > (INT_MAX - digit) / 10 ? INT_MAX : value * 10 + digit;
    }
    return value;
}

/* All ordinary target fields fit in 32 bits. Avoid introducing 64-bit division
 * runtime helpers for the rare ll/j case by dividing two 32-bit halves. */
static unsigned digit10(uint64_t *value)
{
    uint32_t high = (uint32_t)(*value >> 32), low = (uint32_t)*value;
    uint32_t quotient_low = 0, remainder = high % 10u;
    unsigned bit;
    if (!high) {
        *value = low / 10u;
        return low % 10u;
    }
    for (bit = 32; bit; --bit) {
        remainder = remainder * 2u + ((low >> (bit - 1u)) & 1u);
        if (remainder >= 10u) { remainder -= 10u; quotient_low |= 1u << (bit - 1u); }
    }
    *value = ((uint64_t)(high / 10u) << 32) | quotient_low;
    return remainder;
}

enum { LEN_INT, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T, LEN_BIG_L };

int __wrap_vsnprintf(char *data, size_t size, const char *format, va_list input)
{
    output_t out = { data, size, 0 };
    va_list args;
    va_copy(args, input);
    while (*format) {
        int left = 0, plus = 0, space = 0, alternate = 0, zero = 0;
        int width = 0, precision = -1, length = LEN_INT;
        char spec, prefix[2], digits[22];
        size_t prefix_count = 0, count = 0, padding, zeros = 0;
        const char *text = digits;
        uint64_t value = 0;
        unsigned base = 10;
        if (*format != '%') { repeat(&out, *format++, 1); continue; }
        ++format;
        for (;;) {
            if (*format == '-') left = 1;
            else if (*format == '+') plus = 1;
            else if (*format == ' ') space = 1;
            else if (*format == '#') alternate = 1;
            else if (*format == '0') zero = 1;
            else break;
            ++format;
        }
        if (*format == '*') {
            ++format; width = va_arg(args, int);
            if (width < 0) { left = 1; width = width == INT_MIN ? INT_MAX : -width; }
        } else width = decimal(&format);
        if (*format == '.') {
            ++format;
            if (*format == '*') { ++format; precision = va_arg(args, int); }
            else precision = decimal(&format);
        }
        if (*format == 'h') { ++format; length = LEN_H; if (*format == 'h') { ++format; length = LEN_HH; } }
        else if (*format == 'l') { ++format; length = LEN_L; if (*format == 'l') { ++format; length = LEN_LL; } }
        else if (*format == 'j') { ++format; length = LEN_J; }
        else if (*format == 'z') { ++format; length = LEN_Z; }
        else if (*format == 't') { ++format; length = LEN_T; }
        else if (*format == 'L') { ++format; length = LEN_BIG_L; }
        spec = *format;
        if (spec) ++format;
        if (spec == 'd' || spec == 'i') {
            int64_t signed_value;
            if (length == LEN_LL) signed_value = va_arg(args, long long);
            else if (length == LEN_L) signed_value = va_arg(args, long);
            else if (length == LEN_J) signed_value = va_arg(args, intmax_t);
            else if (length == LEN_Z || length == LEN_T) signed_value = va_arg(args, ptrdiff_t);
            else { signed_value = va_arg(args, int); if (length == LEN_H) signed_value = (short)signed_value; else if (length == LEN_HH) signed_value = (signed char)signed_value; }
            value = (uint64_t)signed_value;
            if (signed_value < 0) { prefix[prefix_count++] = '-'; value = 0u - value; }
            else if (plus || space) prefix[prefix_count++] = plus ? '+' : ' ';
        } else if (spec == 'u' || spec == 'o' || spec == 'x' || spec == 'X' || spec == 'p') {
            if (spec == 'p') value = (uintptr_t)va_arg(args, void *);
            else if (length == LEN_LL) value = va_arg(args, unsigned long long);
            else if (length == LEN_L) value = va_arg(args, unsigned long);
            else if (length == LEN_J) value = va_arg(args, uintmax_t);
            else if (length == LEN_Z) value = va_arg(args, size_t);
            else if (length == LEN_T) value = va_arg(args, uintptr_t);
            else { value = va_arg(args, unsigned); if (length == LEN_H) value = (unsigned short)value; else if (length == LEN_HH) value = (unsigned char)value; }
            base = spec == 'o' ? 8u : spec == 'u' ? 10u : 16u;
            if (spec == 'p' || (alternate && value && base == 16u)) { prefix[0] = '0'; prefix[1] = spec == 'X' ? 'X' : 'x'; prefix_count = 2; }
        } else if (spec == 'n') {
            if (length == LEN_HH) *va_arg(args, signed char *) = (signed char)out.count;
            else if (length == LEN_H) *va_arg(args, short *) = (short)out.count;
            else if (length == LEN_L) *va_arg(args, long *) = (long)out.count;
            else if (length == LEN_LL) *va_arg(args, long long *) = (long long)out.count;
            else if (length == LEN_J) *va_arg(args, intmax_t *) = (intmax_t)out.count;
            else if (length == LEN_Z || length == LEN_T) *va_arg(args, ptrdiff_t *) = (ptrdiff_t)out.count;
            else *va_arg(args, int *) = (int)out.count;
            continue;
        } else {
            if (spec == 's') {
                text = va_arg(args, const char *); if (!text) text = "(null)";
                while (text[count] && (precision < 0 || count < (size_t)precision)) ++count;
            } else if (spec == 'f' || spec == 'F' || spec == 'e' || spec == 'E' || spec == 'g' || spec == 'G' || spec == 'a' || spec == 'A') {
                if (length == LEN_BIG_L) (void)va_arg(args, long double);
                else (void)va_arg(args, double);
                text = "<float>"; count = 7;
            } else if (spec == 'c') { digits[count++] = (char)va_arg(args, int); }
            else if (spec == '%') { digits[count++] = '%'; }
            else { digits[count++] = '%'; if (spec) digits[count++] = spec; }
            padding = width > (int)count ? (size_t)width - count : 0;
            if (!left) repeat(&out, ' ', padding);
            string(&out, text, count);
            if (left) repeat(&out, ' ', padding);
            continue;
        }
        if (value || precision != 0) {
            do {
                unsigned digit = base == 10u ? digit10(&value) : (unsigned)value & (base - 1u);
                if (base != 10u) value >>= base == 8u ? 3u : 4u;
                digits[count++] = digit < 10u ? '0' + digit : (spec == 'X' ? 'A' : 'a') + digit - 10u;
            } while (value);
        }
        if (precision > (int)count) zeros = (size_t)precision - count;
        if (alternate && base == 8u && (!count || (digits[count - 1u] != '0' && !zeros))) ++zeros;
        padding = (size_t)width > prefix_count + zeros + count ? (size_t)width - prefix_count - zeros - count : 0;
        if (zero && !left && precision < 0) { zeros += padding; padding = 0; }
        if (!left) repeat(&out, ' ', padding);
        string(&out, prefix, prefix_count); repeat(&out, '0', zeros);
        while (count) repeat(&out, digits[--count], 1);
        if (left) repeat(&out, ' ', padding);
    }
    va_end(args);
    if (out.size) out.data[out.count < out.size ? out.count : out.size - 1u] = 0;
    return out.count > INT_MAX ? -1 : (int)out.count;
}

int __wrap_snprintf(char *data, size_t size, const char *format, ...)
{
    int result; va_list args; va_start(args, format);
    result = __wrap_vsnprintf(data, size, format, args); va_end(args); return result;
}

int __wrap_vsprintf(char *data, const char *format, va_list args)
{
    return __wrap_vsnprintf(data, (size_t)-1, format, args);
}

int __wrap_sprintf(char *data, const char *format, ...)
{
    int result; va_list args; va_start(args, format);
    result = __wrap_vsprintf(data, format, args); va_end(args); return result;
}

/* SDK log.c uses this public cursor/last-character ABI. Its end pointer is the
 * location reserved for NUL; return the number actually stored as stock does. */
int fm1_doom_sdk_print(char **cursor, char *last, const char *format, va_list args)
{
    size_t size, written;
    int result;
    if (!cursor) return 0; /* The pinned target has no console output transport. */
    size = last ? (size_t)(last - *cursor) + 1u : (size_t)-1;
    result = __wrap_vsnprintf(*cursor, size, format, args);
    if (result < 0) return result;
    written = (size_t)result;
    if (written >= size) written = size - 1u;
    *cursor += written;
    return (int)written;
}

#ifndef FM1_DOOM_PRINTF_HOST_TEST
/* printf/vprintf are the existing strong no-ops in baseline debug.c.o. */
void perror(const char *message) { (void)message; }
#endif
