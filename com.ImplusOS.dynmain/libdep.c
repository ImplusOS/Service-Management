/* libdep.c — leaf dependency of the dynamic-linker test chain.
   Built with -mtls-dialect=gnu2 (TLS descriptor access) and
   --hash-style=gnu (GNU hash only, no SysV .hash). */

__thread int dep_tls = 123;
__thread int dep_local = 222;
int dep_ctor_value = 0;

int dep_level(void)
{
    return 1;
}

/* With the gnu2 dialect this access goes through a TLS descriptor
   (R_X86_64_TLSDESC patched by the runtime linker). */
int dep_ie_get(void)
{
    return dep_local;
}

int dep_ctor_value_get(void)
{
    return dep_ctor_value;
}

__attribute__((constructor)) static void dep_ctor(void)
{
    dep_ctor_value = 777;
}
