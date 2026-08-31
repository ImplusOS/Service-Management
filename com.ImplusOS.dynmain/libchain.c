/* libchain.c — depends on libdep.so (recursive DT_NEEDED test).
   Linked with -z now (eager PLT binding, DT_FLAGS_1 BIND_NOW).
   Uses default TLS model: the access to dep_tls below is a cross-DSO
   general-dynamic access (DTPMOD/DTPOFF + __tls_get_addr). */

extern int dep_level(void);
extern int dep_ie_get(void);
extern int preload_marker(void); /* undefined at link time; provided by
                                    libpreload.so via LD_PRELOAD */
extern __thread int dep_tls;

/* initial-exec model for this variable: access below becomes a
   GOTTPOFF relocation even in a shared object. */
__thread int chain_ie_var __attribute__((tls_model("initial-exec"))) = 31;

int chain_counter = 41;

int chain_bump(void)
{
    return ++chain_counter;
}

int chain_level(void)
{
    return 1 + dep_level();
}

int chain_tls_read(void)
{
    return dep_tls;
}

int chain_dep_ie(void)
{
    return dep_ie_get();
}

int chain_ie_local(void)
{
    return chain_ie_var;
}

int chain_preload_marker(void)
{
    return preload_marker();
}
