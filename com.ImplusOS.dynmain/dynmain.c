/* dynmain.c — dynamically-linked Linux-ABI test application.
   Exercises the ImplusOS ld.so: recursive DT_NEEDED, GD/IE/LE TLS,
   COPY relocations, GNU hash lookup and DT_INIT_ARRAY. */

#include <stdint.h>

extern int64_t syscall3(uint64_t nr, uint64_t a, uint64_t b, uint64_t c);

extern const char *dynlib_greet(void);
extern int chain_level(void);
extern int chain_tls_read(void);
extern int chain_dep_ie(void);
extern int chain_ie_local(void);
extern int dep_ie_get(void);
extern int dep_ctor_value_get(void);
extern int chain_bump(void);
extern int chain_preload_marker(void); /* libchain -> libpreload.so (LD_PRELOAD) */
extern int extra_value(void);    /* provided by libextra.so (LD_LIBRARY_PATH) */
extern int chain_counter; /* COPY relocation target (defined in libchain.so) */

static __thread int main_tls = 5;

static int64_t sc_write(int64_t fd, const void *buf, uint64_t n)
{
    return syscall3(1, (uint64_t)fd, (uint64_t)buf, n);
}

static int64_t sc_exit(int64_t code)
{
    return syscall3(231, (uint64_t)code, 0, 0);
}

static uint64_t my_strlen(const char *s)
{
    uint64_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

static int my_strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return (int)((uint8_t)*a - (uint8_t)*b);
}

static char g_num[32];

static const char *itodec(int64_t v)
{
    int64_t x = v;
    if (x < 0) {
        x = -x;
    }
    int i = 30;
    g_num[i] = '\0';
    if (x == 0) {
        g_num[--i] = '0';
    }
    while (x > 0 && i > 0) {
        g_num[--i] = (char)('0' + (int)(x % 10));
        x /= 10;
    }
    if (v < 0) {
        g_num[--i] = '-';
    }
    return &g_num[i];
}

static int g_fail = 0;

static void report(const char *name, int got, int expected)
{
    static const char ok[] = "[dynmain] PASS ";
    static const char bad[] = "[dynmain] FAIL ";
    static const char sp[] = "=";
    static const char nl[] = "\n";
    if (got == expected) {
        (void)sc_write(1, ok, sizeof(ok) - 1u);
        (void)sc_write(1, name, my_strlen(name));
        (void)sc_write(1, nl, sizeof(nl) - 1u);
    } else {
        g_fail = 1;
        (void)sc_write(1, bad, sizeof(bad) - 1u);
        (void)sc_write(1, name, my_strlen(name));
        (void)sc_write(1, sp, sizeof(sp) - 1u);
        (void)sc_write(1, itodec(got), my_strlen(itodec(got)));
        (void)sc_write(1, " expected=", my_strlen(" expected="));
        (void)sc_write(1, itodec(expected), my_strlen(itodec(expected)));
        (void)sc_write(1, nl, sizeof(nl) - 1u);
    }
}

void dynmain_main(uint64_t *stack)
{
    (void)stack;
    static const char pre[] = "[dynmain] ";
    static const char nl[] = "\n";

    const char *msg = dynlib_greet();
    (void)sc_write(1, pre, sizeof(pre) - 1u);
    (void)sc_write(1, msg, my_strlen(msg));
    (void)sc_write(1, nl, sizeof(nl) - 1u);

    report("greet string", my_strcmp(msg, "hello from libdynlib.so!") == 0,
           1);
    report("greet 2nd call (lazy GOT patched)",
           my_strcmp(dynlib_greet(), "hello from libdynlib.so!") == 0, 1);
    report("recursive DT_NEEDED (chain_level)", chain_level(), 2);
    report("cross-DSO GD TLS (chain_tls_read)", chain_tls_read(), 123);
    report("IE TLS via GOTTPOFF (chain_ie_local)", chain_ie_local(), 31);
    report("TLS via TLSDESC (dep_ie_get)", dep_ie_get(), 222);
    report("IE TLS direct (chain_dep_ie)", chain_dep_ie(), 222);
    report("DT_INIT_ARRAY (dep_ctor_value)", dep_ctor_value_get(), 777);
    report("main exe LE TLS (main_tls)", main_tls, 5);
    report("LD_PRELOAD (preload_marker)", chain_preload_marker(), 777);
    report("LD_LIBRARY_PATH (extra_value)", extra_value(), 555);

    report("COPY initial (chain_counter)", chain_counter, 41);
    report("COPY via lib (chain_bump)", chain_bump(), 42);
    report("COPY visible in main (chain_counter)", chain_counter, 42);

    if (g_fail) {
        (void)sc_write(1, "[dynmain] FAILED\n", my_strlen("[dynmain] FAILED\n"));
        (void)sc_exit(1);
    }
    (void)sc_write(1, "[dynmain] ALL PASSED\n",
                   my_strlen("[dynmain] ALL PASSED\n"));
    (void)sc_exit(0);
    for (;;) {
    }
}
