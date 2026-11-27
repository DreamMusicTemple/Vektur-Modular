// wasm_rt.cpp — the few C/C++ runtime pieces the kernel needs when built for
// the browser (wasm32, no libc): a heap, memory/string functions, a small
// snprintf, complex helpers, libstdc++ out-of-line bits and the
// float maths. Double-precision maths and the clock come from JavaScript
// (imports "env.*"); the float functions the DSP calls per sample are
// implemented here so the audio thread never leaves WebAssembly.
//
// Built with: -fno-builtin -mbulk-memory (memcpy/memset become memory.copy/fill).
#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <new>
#include <utility>
#include <chrono>
#include <unordered_map>

#define IMPORT(name) __attribute__((import_module("env"), import_name(#name)))
#define EXPORT(name) __attribute__((export_name(#name)))

extern "C" {
IMPORT(js_sin) double js_sin(double);
IMPORT(js_cos) double js_cos(double);
IMPORT(js_tan) double js_tan(double);
IMPORT(js_exp) double js_exp(double);
IMPORT(js_log) double js_log(double);
IMPORT(js_pow) double js_pow(double, double);
IMPORT(js_atan2) double js_atan2(double, double);
IMPORT(js_tanh) double js_tanh(double);
IMPORT(js_cosh) double js_cosh(double);
IMPORT(js_now) double js_now();
IMPORT(js_abort) void js_abort(int);
IMPORT(js_log_text) void js_log_text(const char*, int);
}

// ---------------------------------------------------------------------------
// Heap: first-fit free list with coalescing, grown with memory.grow.
// ---------------------------------------------------------------------------
extern unsigned char __heap_base;
namespace {
struct Block { size_t size; Block* next; bool free; };   // size includes the header
constexpr size_t kAlign = 16, kHdr = (sizeof(Block) + kAlign - 1) & ~(kAlign - 1);
Block* gHead = nullptr;
uintptr_t gEnd = 0;

inline size_t up(size_t n, size_t a) { return (n + a - 1) & ~(a - 1); }

Block* grow(size_t need) {
    if (!gEnd) gEnd = up(reinterpret_cast<uintptr_t>(&__heap_base), kAlign);
    const uintptr_t memEnd = __builtin_wasm_memory_size(0) * 65536u;
    if (gEnd + need > memEnd) {
        const size_t pages = (gEnd + need - memEnd + 65535) / 65536;
        if (__builtin_wasm_memory_grow(0, pages) == static_cast<size_t>(-1)) return nullptr;
    }
    auto* b = reinterpret_cast<Block*>(gEnd);
    b->size = need; b->free = false; b->next = nullptr;
    gEnd += need;
    // append
    if (!gHead) gHead = b; else { Block* t = gHead; while (t->next) t = t->next; t->next = b; }
    return b;
}

void* alloc(size_t n) {
    const size_t need = up(n ? n : 1, kAlign) + kHdr;
    for (Block* b = gHead; b; b = b->next) {
        if (!b->free || b->size < need) continue;
        if (b->size >= need + kHdr + 64) {   // split
            auto* rest = reinterpret_cast<Block*>(reinterpret_cast<unsigned char*>(b) + need);
            rest->size = b->size - need; rest->free = true; rest->next = b->next;
            b->size = need; b->next = rest;
        }
        b->free = false;
        return reinterpret_cast<unsigned char*>(b) + kHdr;
    }
    Block* b = grow(need);
    return b ? reinterpret_cast<unsigned char*>(b) + kHdr : nullptr;
}

void release(void* p) {
    if (!p) return;
    auto* b = reinterpret_cast<Block*>(static_cast<unsigned char*>(p) - kHdr);
    b->free = true;
    // coalesce with following free blocks (contiguous by construction of the list order)
    for (Block* x = gHead; x; x = x->next)
        while (x->free && x->next && x->next->free &&
               reinterpret_cast<unsigned char*>(x) + x->size == reinterpret_cast<unsigned char*>(x->next)) {
            x->size += x->next->size; x->next = x->next->next;
        }
}

// Aligned allocations keep the original pointer just below the aligned one.
void* allocAligned(size_t n, size_t a) {
    if (a <= kAlign) return alloc(n);
    auto* raw = static_cast<unsigned char*>(alloc(n + a + sizeof(void*)));
    if (!raw) return nullptr;
    auto p = up(reinterpret_cast<uintptr_t>(raw) + sizeof(void*), a);
    reinterpret_cast<void**>(p)[-1] = raw;
    return reinterpret_cast<void*>(p);
}
void releaseAligned(void* p) { if (p) release(reinterpret_cast<void**>(p)[-1]); }
} // namespace

extern "C" {
EXPORT(malloc) void* malloc(size_t n) { return alloc(n); }
EXPORT(free) void free(void* p) { release(p); }
void* calloc(size_t a, size_t b) { void* p = alloc(a * b); if (p) __builtin_memset(p, 0, a * b); return p; }
void abort() { js_abort(0); __builtin_trap(); }

void* memcpy(void* d, const void* s, size_t n) { __builtin_memcpy(d, s, n); return d; }
void* memmove(void* d, const void* s, size_t n) { __builtin_memmove(d, s, n); return d; }
void* memset(void* d, int c, size_t n) { __builtin_memset(d, c, n); return d; }
int memcmp(const void* a, const void* b, size_t n) {
    auto* x = static_cast<const unsigned char*>(a); auto* y = static_cast<const unsigned char*>(b);
    for (size_t i = 0; i < n; ++i) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}
size_t strlen(const char* s) { size_t n = 0; while (s[n]) ++n; return n; }
}

void* operator new(size_t n) { void* p = alloc(n); if (!p) abort(); return p; }
void* operator new[](size_t n) { return operator new(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return alloc(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return alloc(n); }
void* operator new(size_t n, std::align_val_t a) { void* p = allocAligned(n, static_cast<size_t>(a)); if (!p) abort(); return p; }
void* operator new(size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocAligned(n, static_cast<size_t>(a)); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, size_t) noexcept { release(p); }
void operator delete[](void* p, size_t) noexcept { release(p); }
void operator delete(void* p, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { releaseAligned(p); }

namespace std {
const nothrow_t nothrow{};
void __throw_length_error(const char*) { abort(); }
void __throw_bad_alloc() { abort(); }
void __throw_bad_array_new_length() { abort(); }
namespace chrono { inline namespace _V2 {
steady_clock::time_point steady_clock::now() noexcept {
    return time_point(duration(static_cast<int64_t>(js_now() * 1e6)));   // ms → ns
}
} }
namespace __detail {
// libstdc++'s hash-table growth policy (out of line in libstdc++.so).
static size_t nextPrime(size_t n) {
    if (n < 2) return 2;
    for (;; ++n) {
        bool prime = true;
        for (size_t d = 2; d * d <= n; ++d) if (n % d == 0) { prime = false; break; }
        if (prime) return n;
    }
}
std::size_t _Prime_rehash_policy::_M_next_bkt(std::size_t n) const {
    const size_t p = nextPrime(n);
    _M_next_resize = static_cast<size_t>(__builtin_floor(static_cast<double>(p) * static_cast<double>(_M_max_load_factor)));
    return p;
}
std::pair<bool, std::size_t> _Prime_rehash_policy::_M_need_rehash(std::size_t nBkt, std::size_t nElt, std::size_t nIns) const {
    if (nElt + nIns > _M_next_resize) {
        const size_t base = nElt + nIns > (_M_next_resize ? 0u : 11u) ? nElt + nIns : 11u;
        const double minBkts = static_cast<double>(base) / static_cast<double>(_M_max_load_factor);
        if (minBkts >= static_cast<double>(nBkt)) {
            const size_t want = static_cast<size_t>(__builtin_floor(minBkts)) + 1;
            return {true, _M_next_bkt(want > nBkt * 2 ? want : nBkt * 2)};
        }
        _M_next_resize = static_cast<size_t>(__builtin_floor(static_cast<double>(nBkt) * static_cast<double>(_M_max_load_factor)));
    }
    return {false, 0};
}
} // namespace __detail
} // namespace std

// ---------------------------------------------------------------------------
// Maths. Doubles via JS (exact to the last bit, like libm). Floats in-module.
// ---------------------------------------------------------------------------
extern "C" {
double sin(double x) { return js_sin(x); }
double cos(double x) { return js_cos(x); }
double tan(double x) { return js_tan(x); }
double exp(double x) { return js_exp(x); }
double log(double x) { return js_log(x); }
double pow(double a, double b) { return js_pow(a, b); }
double exp2(double x) { return js_pow(2.0, x); }
double log10(double x) { return js_log(x) * 0.43429448190325176; }
double log2(double x) { return js_log(x) * 1.4426950408889634; }
double fmod(double a, double b) { return b == 0.0 ? __builtin_nan("") : a - __builtin_trunc(a / b) * b; }
double sqrt(double x) { return __builtin_sqrt(x); }
double floor(double x) { return __builtin_floor(x); }
double ceil(double x) { return __builtin_ceil(x); }
double atan2(double y, double x) { return js_atan2(y, x); }
long lround(double x) { return static_cast<long>(x < 0 ? __builtin_ceil(x - 0.5) : __builtin_floor(x + 0.5)); }
long long llround(double x) { return static_cast<long long>(x < 0 ? __builtin_ceil(x - 0.5) : __builtin_floor(x + 0.5)); }
long lroundf(float x) { return lround(static_cast<double>(x)); }
float roundf(float x) { return static_cast<float>(x < 0 ? __builtin_ceil(static_cast<double>(x) - 0.5) : __builtin_floor(static_cast<double>(x) + 0.5)); }
float fmaxf(float a, float b) { return a != a ? b : (b != b ? a : (a > b ? a : b)); }
float fminf(float a, float b) { return a != a ? b : (b != b ? a : (a < b ? a : b)); }
float sqrtf(float x) { return __builtin_sqrtf(x); }
float floorf(float x) { return __builtin_floorf(x); }
float ceilf(float x) { return __builtin_ceilf(x); }

// Float functions: argument reduction + polynomials, accurate to ~1 ulp of
// float over the ranges the DSP uses (computed in double).
static double expD(double x) {
    // e^x = 2^k · e^r, |r| ≤ ln2/2
    if (x > 709.0) return __builtin_inf();
    if (x < -745.0) return 0.0;
    const double k = __builtin_floor(x * 1.4426950408889634 + 0.5);
    const double r = x - k * 0.6931471805599453;
    double p = 1.0 + r * (1.0 + r * (0.5 + r * (1.0 / 6 + r * (1.0 / 24 + r * (1.0 / 120 + r * (1.0 / 720 + r * (1.0 / 5040 + r * (1.0 / 40320 + r / 362880.0))))))));
    union { double d; int64_t i; } u; u.i = static_cast<int64_t>(static_cast<int64_t>(k) + 1023) << 52;
    if (k < -1022) { u.i = static_cast<int64_t>(static_cast<int64_t>(k) + 1023 + 600) << 52; return p * u.d * 2.4099198651028841e-181; }
    return p * u.d;
}
static double logD(double x) {
    if (!(x > 0.0)) return x == 0.0 ? -__builtin_inf() : __builtin_nan("");
    if (x == __builtin_inf()) return x;
    union { double d; int64_t i; } u; u.d = x;
    int64_t e = ((u.i >> 52) & 0x7FF) - 1023;
    if (e == -1023) { u.d = x * 4503599627370496.0; e = ((u.i >> 52) & 0x7FF) - 1023 - 52; }
    u.i = (u.i & 0x000FFFFFFFFFFFFFll) | 0x3FF0000000000000ll;   // m in [1,2)
    double m = u.d;
    if (m > 1.4142135623730951) { m *= 0.5; ++e; }
    const double f = (m - 1.0) / (m + 1.0), f2 = f * f;
    const double s = f * (2.0 + f2 * (2.0 / 3 + f2 * (2.0 / 5 + f2 * (2.0 / 7 + f2 * (2.0 / 9 + f2 * (2.0 / 11 + f2 * (2.0 / 13 + f2 * 2.0 / 15)))))));
    return static_cast<double>(e) * 0.6931471805599453 + s;
}
static double sinD(double x) {   // reduce to [-π/4, π/4]
    const double q = __builtin_floor(x * 0.6366197723675814 + 0.5);
    const double r = (x - q * 1.5707963267341256) - q * 6.077100506506192e-11;
    const double r2 = r * r;
    const double s = r * (1 + r2 * (-1.0 / 6 + r2 * (1.0 / 120 + r2 * (-1.0 / 5040 + r2 * (1.0 / 362880 + r2 * (-1.0 / 39916800))))));
    const double c = 1 + r2 * (-0.5 + r2 * (1.0 / 24 + r2 * (-1.0 / 720 + r2 * (1.0 / 40320 + r2 * (-1.0 / 3628800 + r2 / 479001600.0)))));
    switch (static_cast<int64_t>(q) & 3) { case 0: return s; case 1: return c; case 2: return -s; default: return -c; }
}
static double cosD(double x) { return sinD(x + 1.5707963267948966); }

float expf(float x) { return static_cast<float>(expD(x)); }
float exp2f(float x) { return static_cast<float>(expD(static_cast<double>(x) * 0.6931471805599453)); }
float logf(float x) { return static_cast<float>(logD(x)); }
float log2f(float x) { return static_cast<float>(logD(x) * 1.4426950408889634); }
float log10f(float x) { return static_cast<float>(logD(x) * 0.43429448190325176); }
float powf(float a, float b) {
    if (b == 0.f) return 1.f;
    if (a > 0.f) return static_cast<float>(expD(static_cast<double>(b) * logD(a)));
    if (a == 0.f) return b > 0.f ? 0.f : __builtin_inff();
    const float bi = __builtin_truncf(b);
    if (bi != b) return __builtin_nanf("");
    const float m = static_cast<float>(expD(static_cast<double>(b) * logD(-static_cast<double>(a))));
    return (static_cast<int64_t>(bi) & 1) ? -m : m;
}
float sinf(float x) { return static_cast<float>(__builtin_fabs(x) < 1e5f ? sinD(x) : js_sin(x)); }
float cosf(float x) { return static_cast<float>(__builtin_fabs(x) < 1e5f ? cosD(x) : js_cos(x)); }
float tanf(float x) { const double s = __builtin_fabs(x) < 1e5f ? sinD(x) : js_sin(x), c = __builtin_fabs(x) < 1e5f ? cosD(x) : js_cos(x); return static_cast<float>(s / c); }
float tanhf(float x) {
    const double d = x;
    if (d > 10.0) return 1.f;
    if (d < -10.0) return -1.f;
    if (__builtin_fabs(d) < 0.004) return static_cast<float>(d - d * d * d / 3.0);
    const double e = expD(2.0 * d);
    return static_cast<float>((e - 1.0) / (e + 1.0));
}
float coshf(float x) { const double e = expD(x); return static_cast<float>(0.5 * (e + 1.0 / e)); }
float atan2f(float y, float x) { return static_cast<float>(js_atan2(y, x)); }

// Complex (double _Complex) helpers clang calls for * and /.
struct C { double re, im; };
double cabs(double _Complex z) { return __builtin_sqrt(__real__ z * __real__ z + __imag__ z * __imag__ z); }
double carg(double _Complex z) { return js_atan2(__imag__ z, __real__ z); }
double _Complex __muldc3(double a, double b, double c, double d) { double _Complex r; __real__ r = a * c - b * d; __imag__ r = a * d + b * c; return r; }
double _Complex __divdc3(double a, double b, double c, double d) {
    const double den = c * c + d * d; double _Complex r;
    __real__ r = (a * c + b * d) / den; __imag__ r = (b * c - a * d) / den; return r;
}

// ---------------------------------------------------------------------------
// printf family: %s %c %d %i %u %x %f %g(≈f) %% with flags +-0 space, width, precision.
// ---------------------------------------------------------------------------
static void put(char* out, size_t cap, size_t& n, char c) { if (n + 1 < cap) out[n] = c; ++n; }
int vsnprintf(char* out, size_t cap, const char* f, va_list ap) {
    size_t n = 0;
    for (; *f; ++f) {
        if (*f != '%') { put(out, cap, n, *f); continue; }
        ++f;
        bool plus = false, left = false, zero = false, space = false;
        for (;; ++f) { if (*f == '+') plus = true; else if (*f == '-') left = true; else if (*f == '0') zero = true; else if (*f == ' ') space = true; else break; }
        int width = 0, prec = -1;
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') { ++f; prec = 0; while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
        while (*f == 'l' || *f == 'h' || *f == 'z') ++f;
        char buf[64]; int len = 0;
        switch (*f) {
            case '%': buf[len++] = '%'; break;
            case 'c': buf[len++] = static_cast<char>(va_arg(ap, int)); break;
            case 's': {
                const char* s = va_arg(ap, const char*); if (!s) s = "(null)";
                int sl = 0; while (s[sl] && (prec < 0 || sl < prec)) ++sl;
                if (!left) for (int i = sl; i < width; ++i) put(out, cap, n, ' ');
                for (int i = 0; i < sl; ++i) put(out, cap, n, s[i]);
                if (left) for (int i = sl; i < width; ++i) put(out, cap, n, ' ');
                continue;
            }
            case 'd': case 'i': case 'u': case 'x': {
                long long v = (*f == 'u' || *f == 'x') ? static_cast<long long>(va_arg(ap, unsigned)) : static_cast<long long>(va_arg(ap, int));
                const bool neg = v < 0; unsigned long long u = neg ? static_cast<unsigned long long>(-v) : static_cast<unsigned long long>(v);
                const unsigned base = *f == 'x' ? 16 : 10;
                char t[32]; int tl = 0; do { const unsigned dgt = static_cast<unsigned>(u % base); t[tl++] = static_cast<char>(dgt < 10 ? '0' + dgt : 'a' + dgt - 10); u /= base; } while (u);
                if (neg) buf[len++] = '-'; else if (plus) buf[len++] = '+'; else if (space) buf[len++] = ' ';
                while (tl) buf[len++] = t[--tl];
                break;
            }
            case 'f': case 'g': case 'e': {
                double v = va_arg(ap, double);
                if (prec < 0) prec = 6;
                if (prec > 9) prec = 9;
                if (v != v) { buf[len++] = 'n'; buf[len++] = 'a'; buf[len++] = 'n'; break; }
                const bool neg = v < 0 || (v == 0 && 1 / v < 0);
                if (neg) v = -v;
                double scale = 1; for (int i = 0; i < prec; ++i) scale *= 10;
                double r = __builtin_floor(v * scale + 0.5);
                if (r > 9e17) { buf[len++] = 'i'; buf[len++] = 'n'; buf[len++] = 'f'; break; }
                unsigned long long whole = static_cast<unsigned long long>(r / scale), frac = static_cast<unsigned long long>(r - static_cast<double>(whole) * scale);
                if (neg && (whole || frac)) buf[len++] = '-'; else if (plus) buf[len++] = '+'; else if (space) buf[len++] = ' ';
                char t[32]; int tl = 0; do { t[tl++] = static_cast<char>('0' + whole % 10); whole /= 10; } while (whole);
                while (tl) buf[len++] = t[--tl];
                if (prec > 0) {
                    buf[len++] = '.';
                    char fr[16]; for (int i = prec - 1; i >= 0; --i) { fr[i] = static_cast<char>('0' + frac % 10); frac /= 10; }
                    for (int i = 0; i < prec; ++i) buf[len++] = fr[i];
                }
                break;
            }
            default: buf[len++] = '?'; break;
        }
        const char pad = zero && !left ? '0' : ' ';
        int start = 0;
        if (pad == '0' && len && (buf[0] == '-' || buf[0] == '+' || buf[0] == ' ')) { put(out, cap, n, buf[0]); start = 1; }
        if (!left) for (int i = len; i < width; ++i) put(out, cap, n, pad);
        for (int i = start; i < len; ++i) put(out, cap, n, buf[i]);
        if (left) for (int i = len; i < width; ++i) put(out, cap, n, ' ');
    }
    if (cap) out[n < cap ? n : cap - 1] = 0;
    return static_cast<int>(n);
}
int snprintf(char* out, size_t cap, const char* f, ...) { va_list ap; va_start(ap, f); const int r = vsnprintf(out, cap, f, ap); va_end(ap); return r; }
struct _IO_FILE; typedef struct _IO_FILE FILE;
FILE* stderr = nullptr;
int fprintf(FILE*, const char* f, ...) {
    char buf[512]; va_list ap; va_start(ap, f); const int r = vsnprintf(buf, sizeof buf, f, ap); va_end(ap);
    js_log_text(buf, r < 511 ? r : 511);
    return r;
}
}
