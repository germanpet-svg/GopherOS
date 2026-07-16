#include "string.h"

void* memset(void* dst, int val, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)val;
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i-1] = s[i-1];
    }
    return dst;
}

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

char* strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == 0) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

size_t utoa(uint32_t val, char* buf, int base) {
    static const char digits[] = "0123456789abcdef";
    char tmp[32];
    size_t i = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return 1; }
    while (val) {
        tmp[i++] = digits[val % base];
        val /= base;
    }
    size_t len = i;
    for (size_t j = 0; j < len; j++) buf[j] = tmp[len - 1 - j];
    buf[len] = 0;
    return len;
}

size_t itoa(int32_t val, char* buf, int base) {
    if (val < 0 && base == 10) {
        buf[0] = '-';
        size_t len = utoa((uint32_t)(-val), buf + 1, base);
        return len + 1;
    }
    return utoa((uint32_t)val, buf, base);
}
