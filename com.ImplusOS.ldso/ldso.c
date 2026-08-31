/* ldso.c — ELF dynamic linker for ImplusOS Linux-ABI processes.
   Freestanding; uses raw Linux syscall numbers only.

   Scope (completion of the PT_INTERP loader):
     - Recursive DT_NEEDED loading (breadth-first, dedup by soname)
     - SysV + GNU hash symbol lookup
     - TLS: PT_TLS capture, static TLS pool + TCB, FS base setup via
       arch_prctl(ARCH_SET_FS), R_X86_64_TPOFF32/TPOFF64/GOTTPOFF
       (initial-exec) and DTPMOD64/DTPOFF64/DTPOFF32 (general-dynamic),
       __tls_get_addr export
     - R_X86_64_COPY
     - DT_INIT / DT_INIT_ARRAY execution (dependencies first)

   TLS layout (mirrors libc/I_libc/src/dlfcn.c, verified against
   x86_64-elf-gcc + binutils):
     - TP = FS base.  The TCB header at [TP..TP+8) holds the self pointer.
     - The main executable's static TLS block sits at negative offsets
       directly below TP; shared object blocks are placed below that,
       each starting at TP - align_up(0x10 + cursor, p_align).
     - Module ids are 1-based in load order (main = 1 when it has PT_TLS). */

#include <stdint.h>
#include <stddef.h>

extern int64_t syscall3(uint64_t nr, uint64_t a, uint64_t b, uint64_t c);
extern int64_t syscall6(uint64_t nr, uint64_t a, uint64_t b, uint64_t c,
                        uint64_t d, uint64_t e, uint64_t f);
extern void ldso_jump(uint64_t entry, uint64_t rsp);

#define SYS_READ        0u
#define SYS_WRITE       1u
#define SYS_OPEN        2u
#define SYS_CLOSE       3u
#define SYS_MMAP        9u
#define SYS_ARCH_PRCTL 158u
#define SYS_EXIT_GROUP 231u

#define O_RDONLY        0u
#define PROT_READ       1u
#define PROT_WRITE      2u
#define PROT_EXEC       4u
#define MAP_PRIVATE     0x2u
#define MAP_ANONYMOUS   0x20u

#define ARCH_SET_FS     0x1002u

#define PAGE_SIZE       4096u

/* ------------------------------------------------------------------ */
/* ELF64 definitions                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct {
    uint64_t d_tag;
    uint64_t d_val;
} Elf64_Dyn;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

#define ELF64_R_SYM(i)  ((uint32_t)((i) >> 32))
#define ELF64_R_TYPE(i) ((uint32_t)((i) & 0xFFFFFFFFu))
#define ELF64_ST_BIND(i) ((i) >> 4)
#define ELF64_ST_TYPE(i) ((i) & 0xFu)

#define PT_LOAD    1u
#define PT_DYNAMIC 2u
#define PT_TLS     7u

#define DT_NULL         0u
#define DT_NEEDED       1u
#define DT_PLTRELSZ     2u
#define DT_PLTGOT       3u
#define DT_HASH         4u
#define DT_STRTAB       5u
#define DT_SYMTAB       6u
#define DT_RELA         7u
#define DT_RELASZ       8u
#define DT_RELAENT      9u
#define DT_SYMENT       11u
#define DT_INIT         12u
#define DT_REL          17u
#define DT_RELSZ        18u
#define DT_RELENT       19u
#define DT_PLTREL       20u
#define DT_JMPREL       23u
#define DT_INIT_ARRAY   25u
#define DT_INIT_ARRAYSZ 27u
#define DT_FLAGS        30u
#define DT_GNU_HASH     0x6FFFFEF5u
#define DT_FLAGS_1      0x6FFFFFFBu

#define DF_BIND_NOW     0x8u
#define DF_1_NOW        0x1u

#define SHN_UNDEF   0u
#define SHN_ABS     0xFFF1u
#define STT_NOTYPE  0u
#define STT_OBJECT  1u
#define STT_FUNC    2u
#define STT_SECTION 3u
#define STT_FILE    4u
#define STT_TLS     6u

#define R_X86_64_NONE       0u
#define R_X86_64_64         1u
#define R_X86_64_PC32       2u
#define R_X86_64_GOT32      3u
#define R_X86_64_PLT32      4u
#define R_X86_64_COPY       5u
#define R_X86_64_GLOB_DAT   6u
#define R_X86_64_JUMP_SLOT  7u
#define R_X86_64_RELATIVE   8u
#define R_X86_64_GOTPCREL   9u
#define R_X86_64_32         10u
#define R_X86_64_32S        11u
#define R_X86_64_DTPMOD64   16u
#define R_X86_64_DTPOFF64   17u
#define R_X86_64_TPOFF64    18u
#define R_X86_64_DTPOFF32   21u
#define R_X86_64_GOTTPOFF   22u
#define R_X86_64_TPOFF32    23u
#define R_X86_64_TLSDESC    36u
#define R_X86_64_TLSDESC_CALL 37u

/* ------------------------------------------------------------------ */
/* Minimal libc helpers                                                */
/* ------------------------------------------------------------------ */

static uint64_t sc_read(int64_t fd, void *buf, uint64_t n)
{
    return (uint64_t)syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, n);
}

static uint64_t sc_write(int64_t fd, const void *buf, uint64_t n)
{
    return (uint64_t)syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, n);
}

static int64_t sc_open(const char *path)
{
    return syscall3(SYS_OPEN, (uint64_t)path, O_RDONLY, 0);
}

static int64_t sc_close(int64_t fd)
{
    return syscall3(SYS_CLOSE, (uint64_t)fd, 0, 0);
}

static void *sc_mmap(uint64_t len)
{
    return (void *)(uintptr_t)syscall6(SYS_MMAP, 0, len,
                                       PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS,
                                       (uint64_t)(-1), 0);
}

static void sc_exit_group(int64_t code)
{
    (void)syscall3(SYS_EXIT_GROUP, (uint64_t)code, 0, 0);
    for (;;) {
    }
}

static void sc_arch_set_fs(uint64_t addr)
{
    (void)syscall3(SYS_ARCH_PRCTL, ARCH_SET_FS, addr, 0);
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

static void my_memcpy(void *dst, const void *src, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n-- != 0) {
        *d++ = *s++;
    }
}

static void my_memset(void *dst, int v, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n-- != 0) {
        *d++ = (uint8_t)v;
    }
}

static uint64_t align_up(uint64_t v, uint64_t a)
{
    if (a < 2u) {
        return v;
    }
    uint64_t m = a - 1u;
    return (v + m) & ~m;
}

static uint32_t my_hash(const char *name)
{
    uint32_t h = 0;
    for (const unsigned char *p = (const unsigned char *)name;
         *p != 0; ++p) {
        h = (h << 4) + *p;
        uint32_t g = h & 0xF0000000u;
        if (g != 0) {
            h ^= g >> 24;
        }
        h &= ~g;
    }
    return h;
}

static uint32_t gnu_hash(const char *name)
{
    uint32_t h = 5381u;
    for (const unsigned char *p = (const unsigned char *)name;
         *p != 0; ++p) {
        h = h * 33u + *p;
    }
    return h;
}

static char g_dbg_buf[96];

static void dbg(const char *s)
{
    (void)sc_write(1, s, my_strlen(s));
}

static void dbg_hex(uint64_t v)
{
    g_dbg_buf[0] = '0';
    g_dbg_buf[1] = 'x';
    for (int i = 15; i >= 0; --i) {
        uint32_t nib = (uint32_t)((v >> (uint32_t)(i * 4)) & 0xFu);
        g_dbg_buf[2 + (15 - i)] = (char)((nib < 10u) ? ('0' + nib)
                                                     : ('a' + nib - 10u));
    }
    g_dbg_buf[18] = '\0';
    dbg(g_dbg_buf);
}

/* ------------------------------------------------------------------ */
/* Dynamic object state                                                */
/* ------------------------------------------------------------------ */

#define MAX_DSOS 32

typedef struct {
    uint64_t bias;      /* 0 for the main executable */
    uint64_t dynamic;   /* vaddr of PT_DYNAMIC (absolute) */
    uint64_t strtab;    /* DT_STRTAB (bias-relative for DSOs) */
    uint64_t symtab;    /* DT_SYMTAB */
    uint64_t nsyms;     /* symbol count */
    uint64_t sysv_hash; /* DT_HASH vaddr, 0 if absent */
    uint64_t gnu_hash;  /* DT_GNU_HASH vaddr, 0 if absent */
    uint64_t reladyn;   /* DT_RELA */
    uint64_t reladynsz; /* DT_RELASZ */
    uint64_t jmprel;    /* DT_JMPREL */
    uint64_t pltrelsz;  /* DT_PLTRELSZ */
    uint64_t pltrelent; /* DT_PLTREL (RELA=7, REL=17) */
    uint64_t pltgot;    /* DT_PLTGOT (GOT base, bias-relative) */
    uint64_t flags;     /* DT_FLAGS / DT_FLAGS_1 (bind-now flags) */
    uint64_t init;      /* DT_INIT, 0 if none */
    uint64_t init_arr;  /* DT_INIT_ARRAY vaddr */
    uint64_t init_arrsz; /* DT_INIT_ARRAYSZ */
    const char *soname; /* pointer into this DSO's strtab */
    /* TLS */
    uint64_t tls_memsz;
    uint64_t tls_filesz;
    uint64_t tls_align;
    uint64_t tls_init;  /* vaddr of the TLS init image (bias-relative) */
    int64_t  tls_rel_tp; /* TLS block offset relative to TP */
    uint64_t modid;     /* TLS module id (0 = none) */
} dso_t;

static dso_t g_dsos[MAX_DSOS];
static int g_ndsos = 0;
static char g_execfn_dir[128];
static uint64_t g_initial_rsp = 0;
static uint64_t g_tls_tp = 0;

static uint64_t dso_vaddr(const dso_t *d, uint64_t v)
{
    return d->bias + v;
}

static uint64_t dso_strtab(const dso_t *d)
{
    return dso_vaddr(d, d->strtab);
}

static uint64_t dso_symtab(const dso_t *d)
{
    return dso_vaddr(d, d->symtab);
}

/* ------------------------------------------------------------------ */
/* ELF loading                                                         */
/* ------------------------------------------------------------------ */

static int load_elf_file(const char *path, dso_t *out)
{
    int64_t fd = sc_open(path);
    if (fd < 0) {
        return -1;
    }

    Elf64_Ehdr ehdr;
    uint64_t got = sc_read(fd, &ehdr, sizeof(ehdr));
    if (got != sizeof(ehdr) ||
        ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F' ||
        ehdr.e_ident[4] != 2 || ehdr.e_machine != 62) {
        (void)sc_close(fd);
        return -2;
    }

    uint64_t phnum = ehdr.e_phnum;
    uint64_t phoff = ehdr.e_phoff;
    uint64_t phent = ehdr.e_phentsize;
    uint64_t ph_bytes = phnum * phent;
    if (phnum == 0 || phnum > 64u || phent < sizeof(Elf64_Phdr) ||
        ph_bytes < sizeof(Elf64_Phdr)) {
        (void)sc_close(fd);
        return -2;
    }

    uint64_t max_offset = phoff + ph_bytes;
    uint64_t min_vaddr = ~0ULL;
    uint64_t max_end = 0;
    uint64_t dyn_vaddr = 0;

    /* read phdrs */
    uint8_t ph_buf[64 * sizeof(Elf64_Phdr)];
    Elf64_Phdr *phdrs = (Elf64_Phdr *)ph_buf;
    {
        uint8_t tmp[64 * sizeof(Elf64_Phdr)];
        uint64_t off = 0;
        while (off < ph_bytes) {
            uint64_t want = ph_bytes - off;
            if (want > sizeof(tmp)) {
                want = sizeof(tmp);
            }
            uint64_t n = sc_read(fd, tmp, want);
            if (n == 0) {
                (void)sc_close(fd);
                return -2;
            }
            my_memcpy((uint8_t *)ph_buf + off, tmp, n);
            off += n;
        }
    }
    /* re-read from the start of the file for segment data */
    (void)sc_close(fd);
    fd = sc_open(path);
    if (fd < 0) {
        return -2;
    }
    (void)sc_read(fd, &ehdr, sizeof(ehdr));

    for (uint64_t i = 0; i < phnum; ++i) {
        Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        if (ph->p_memsz == 0) {
            continue;
        }
        if (ph->p_vaddr < min_vaddr) {
            min_vaddr = ph->p_vaddr;
        }
        uint64_t end = ph->p_vaddr + ph->p_memsz;
        if (end > max_end) {
            max_end = end;
        }
        uint64_t fo = ph->p_offset + ph->p_filesz;
        if (fo > max_offset) {
            max_offset = fo;
        }
    }
    for (uint64_t i = 0; i < phnum; ++i) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            dyn_vaddr = phdrs[i].p_vaddr;
            break;
        }
    }
    if (min_vaddr == ~0ULL || dyn_vaddr == 0) {
        (void)sc_close(fd);
        return -2;
    }

    uint64_t load_len = max_end - min_vaddr;
    uint64_t align_min = min_vaddr & ~(uint64_t)(PAGE_SIZE - 1u);
    uint64_t map_len = load_len + (min_vaddr - align_min);
    map_len = (map_len + PAGE_SIZE - 1u) & ~(uint64_t)(PAGE_SIZE - 1u);

    uint8_t *base = (uint8_t *)sc_mmap(map_len);
    if (base == (uint8_t *)0 || (uintptr_t)base == ~0ULL) {
        (void)sc_close(fd);
        return -3;
    }
    out->bias = (uint64_t)base - align_min;

    /* read the whole file image into a heap buffer, then copy segments */
    {
        uint64_t fb_len = (max_offset + PAGE_SIZE - 1u) &
                          ~(uint64_t)(PAGE_SIZE - 1u);
        uint8_t *fbuf = (uint8_t *)sc_mmap(fb_len);
        if (fbuf == (uint8_t *)0 || (uintptr_t)fbuf == ~0ULL) {
            (void)sc_close(fd);
            return -3;
        }
        (void)sc_close(fd);
        fd = sc_open(path);
        if (fd < 0) {
            return -3;
        }
        uint64_t total = 0;
        while (total < max_offset) {
            uint64_t n = sc_read(fd, fbuf + total, max_offset - total);
            if (n == 0) {
                break;
            }
            total += n;
        }
        (void)sc_close(fd);
        if (total < max_offset) {
            return -2;
        }
        for (uint64_t i = 0; i < phnum; ++i) {
            Elf64_Phdr *ph = &phdrs[i];
            if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
                continue;
            }
            uint8_t *dst = (uint8_t *)(out->bias + ph->p_vaddr);
            my_memcpy(dst, fbuf + ph->p_offset, ph->p_filesz);
            if (ph->p_memsz > ph->p_filesz) {
                my_memset(dst + ph->p_filesz, 0,
                          ph->p_memsz - ph->p_filesz);
            }
        }
    }

    out->dynamic = out->bias + dyn_vaddr;
    out->strtab = 0;
    out->symtab = 0;
    out->nsyms = 0;
    out->sysv_hash = 0;
    out->gnu_hash = 0;
    out->reladyn = 0;
    out->reladynsz = 0;
    out->jmprel = 0;
    out->pltrelsz = 0;
    out->pltrelent = 0;
    out->pltgot = 0;
    out->flags = 0;
    out->init = 0;
    out->init_arr = 0;
    out->init_arrsz = 0;
    out->tls_memsz = 0;
    out->tls_filesz = 0;
    out->tls_align = 16;
    out->tls_init = 0;
    out->tls_rel_tp = 0;
    out->modid = 0;

    for (uint64_t i = 0; i < phnum; ++i) {
        Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type == PT_TLS) {
            out->tls_memsz = ph->p_memsz;
            out->tls_filesz = ph->p_filesz;
            out->tls_align = ph->p_align;
            out->tls_init = ph->p_vaddr;
            break;
        }
    }

    const Elf64_Dyn *dyn = (const Elf64_Dyn *)(uintptr_t)out->dynamic;
    while (dyn->d_tag != DT_NULL) {
        switch (dyn->d_tag) {
            case DT_NEEDED:
                if (out->soname == NULL) {
                    out->soname = (const char *)(uintptr_t)
                        (out->bias + out->strtab + dyn->d_val);
                }
                break;
            case DT_STRTAB:
                out->strtab = dyn->d_val;
                break;
            case DT_SYMTAB:
                out->symtab = dyn->d_val;
                break;
            case DT_HASH:
                out->sysv_hash = out->bias + dyn->d_val;
                break;
            case DT_GNU_HASH:
                out->gnu_hash = out->bias + dyn->d_val;
                break;
            case DT_RELA:
                out->reladyn = out->bias + dyn->d_val;
                break;
            case DT_RELASZ:
                out->reladynsz = dyn->d_val;
                break;
            case DT_JMPREL:
                out->jmprel = out->bias + dyn->d_val;
                break;
            case DT_PLTRELSZ:
                out->pltrelsz = dyn->d_val;
                break;
            case DT_PLTREL:
                out->pltrelent = dyn->d_val;
                break;
            case DT_PLTGOT:
                out->pltgot = dyn->d_val;
                break;
            case DT_FLAGS:
                out->flags |= dyn->d_val;
                break;
            case DT_FLAGS_1:
                out->flags |= dyn->d_val;
                break;
            case DT_INIT:
                out->init = out->bias + dyn->d_val;
                break;
            case DT_INIT_ARRAY:
                out->init_arr = out->bias + dyn->d_val;
                break;
            case DT_INIT_ARRAYSZ:
                out->init_arrsz = dyn->d_val;
                break;
            default:
                break;
        }
        ++dyn;
    }

    if (out->strtab == 0 || out->symtab == 0) {
        dbg("[ldso] dso missing strtab/symtab\n");
        return -4;
    }
    if (out->sysv_hash != 0) {
        const uint32_t *h = (const uint32_t *)(uintptr_t)out->sysv_hash;
        out->nsyms = h[1];
    } else if (out->gnu_hash != 0) {
        const uint32_t *h = (const uint32_t *)(uintptr_t)out->gnu_hash;
        uint32_t nbuckets = h[0];
        uint32_t symoffset = h[1];
        uint32_t nchain = 0;
        const uint32_t *buckets = h + 4 + (h[2] * sizeof(uint64_t)) / 4u;
        const uint32_t *chain = buckets + nbuckets;
        for (uint32_t b = 0; b < nbuckets; ++b) {
            uint32_t idx = buckets[b];
            if (idx < symoffset) {
                continue;
            }
            while ((chain[idx - symoffset] & 1u) == 0) {
                ++idx;
            }
            if (idx + 1u > nchain) {
                nchain = idx + 1u;
            }
        }
        out->nsyms = nchain;
    } else {
        dbg("[ldso] dso has no hash table\n");
        return -4;
    }
    if (out->nsyms == 0 || out->nsyms > 1000000u) {
        dbg("[ldso] dso bad nsyms\n");
        return -4;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Symbol resolution                                                   */
/* ------------------------------------------------------------------ */

/* The x86-64 TLS ABI: __tls_get_addr(struct tls_index *ti) where
   tls_index is { uint64_t ti_module; uint64_t ti_offset; }. */
typedef struct {
    uint64_t ti_module;
    uint64_t ti_offset;
} ldso_tls_index;

static uint64_t ldso_tls_get_addr_impl(ldso_tls_index *ti);
/* Find the defining (dso, sym) pair for relocation processing.
   Returns NULL if the symbol cannot be resolved. */
static const Elf64_Sym *resolve_def(const dso_t *d, uint32_t symidx,
                                    const dso_t **owner_out)
{
    const Elf64_Sym *symtab = (const Elf64_Sym *)(uintptr_t)dso_symtab(d);
    const char *strtab = (const char *)(uintptr_t)dso_strtab(d);
    *owner_out = NULL;
    if (symidx >= d->nsyms) {
        return NULL;
    }
    const Elf64_Sym *sym = &symtab[symidx];
    if (sym->st_shndx != SHN_UNDEF &&
        ((sym->st_info >> 4u) == 0u /* STB_LOCAL */)) {
        *owner_out = d;
        return sym;
    }
    if (sym->st_name == 0) {
        return NULL;
    }
    const char *name = strtab + sym->st_name;
    uint32_t hash = my_hash(name);
    uint32_t ghash = gnu_hash(name);
    for (int i = 0; i < g_ndsos; ++i) {
        const dso_t *cand = &g_dsos[i];
        const Elf64_Sym *st = (const Elf64_Sym *)(uintptr_t)dso_symtab(cand);
        const char *sstr = (const char *)(uintptr_t)dso_strtab(cand);
        uint64_t n = cand->nsyms;

        if (cand->sysv_hash != 0) {
            const uint32_t *h = (const uint32_t *)(uintptr_t)cand->sysv_hash;
            uint32_t nbucket = h[0];
            if (nbucket != 0) {
                const uint32_t *buckets = h + 2;
                const uint32_t *chains = buckets + nbucket;
                uint32_t idx = buckets[hash % nbucket];
                while (idx != 0) {
                    const Elf64_Sym *s2 = &st[idx];
                    if (s2->st_name != 0 && s2->st_shndx != SHN_UNDEF) {
                        uint32_t t = ELF64_ST_TYPE(s2->st_info);
                        if (t != STT_NOTYPE && t != STT_SECTION &&
                            t != STT_FILE) {
                            if (my_strcmp(name, sstr + s2->st_name) == 0) {
                                *owner_out = cand;
                                return s2;
                            }
                        }
                    }
                    idx = chains[idx];
                }
                continue;
            }
            /* Degenerate hash (nbucket == 0): no symbols are reachable
               through it; fall through to the linear scan. */
        }
        if (cand->gnu_hash != 0) {
            const uint32_t *h = (const uint32_t *)(uintptr_t)cand->gnu_hash;
            uint32_t nbuckets = h[0];
            uint32_t symoffset = h[1];
            uint32_t bloom_size = h[2];
            const uint64_t *bloom = (const uint64_t *)(h + 4);
            const uint32_t *buckets = (const uint32_t *)(bloom + bloom_size);
            const uint32_t *chain = buckets + nbuckets;
            uint32_t idx = buckets[ghash % nbuckets];
            if (idx < symoffset) {
                continue;
            }
            for (;;) {
                const Elf64_Sym *s2 = &st[idx];
                uint32_t c = chain[idx - symoffset];
                if ((c | 1u) == (ghash | 1u) && s2->st_name != 0 &&
                    s2->st_shndx != SHN_UNDEF) {
                    uint32_t t = ELF64_ST_TYPE(s2->st_info);
                    if (t != STT_NOTYPE && t != STT_SECTION &&
                        t != STT_FILE) {
                        if (my_strcmp(name, sstr + s2->st_name) == 0) {
                            *owner_out = cand;
                            return s2;
                        }
                    }
                }
                if ((c & 1u) != 0) {
                    break;
                }
                ++idx;
            }
            continue;
        }
        for (uint64_t i2 = 0; i2 < n; ++i2) {
            const Elf64_Sym *s2 = &st[i2];
            if (s2->st_name == 0 || s2->st_shndx == SHN_UNDEF) {
                continue;
            }
            uint32_t t = ELF64_ST_TYPE(s2->st_info);
            if (t == STT_NOTYPE || t == STT_SECTION || t == STT_FILE) {
                continue;
            }
            if (my_strcmp(name, sstr + s2->st_name) == 0) {
                *owner_out = cand;
                return s2;
            }
        }
    }
    if (my_strcmp(name, "__tls_get_addr") == 0) {
        static Elf64_Sym pseudo;
        pseudo.st_name = 0;
        pseudo.st_info = (uint8_t)((STT_FUNC << 0) | 0);
        pseudo.st_other = 0;
        pseudo.st_shndx = SHN_ABS;
        pseudo.st_value = (uint64_t)ldso_tls_get_addr_impl;
        pseudo.st_size = 0;
        *owner_out = NULL;
        return &pseudo;
    }
    return NULL;
}

static uint64_t sym_value(const dso_t *d, uint32_t symidx)
{
    const dso_t *owner = NULL;
    const Elf64_Sym *sym = resolve_def(d, symidx, &owner);
    if (sym == NULL) {
        return 0;
    }
    if (owner == NULL) {
        return sym->st_value;
    }
    return owner->bias + sym->st_value;
}

/* ------------------------------------------------------------------ */
/* TLS setup                                                           */
/* ------------------------------------------------------------------ */

#define TLS_MAX_MODULES 32

typedef struct {
    int64_t  rel_tp;
    uint64_t memsz;
    uint64_t filesz;
    uint64_t align;
    uint64_t init_off; /* init image address (absolute, bias applied) */
} tls_module_t;

static tls_module_t g_tls_modules[TLS_MAX_MODULES];
static int g_tls_module_count = 0;
static uint64_t g_tls_cursor = 0;
static int g_tls_main_has_tls = 0;

static uint64_t tls_pool_total(uint64_t cursor)
{
    return 0x10u + 0x10u + cursor;
}

static void tls_register(uint64_t init_addr, uint64_t memsz, uint64_t filesz,
                         uint64_t align, int64_t *rel_out)
{
    if (align < 16u) {
        align = 16u;
    }
    int64_t rel = -(int64_t)align_up(0x10u + g_tls_cursor, align);
    g_tls_cursor = (uint64_t)(-rel) + memsz;
    if (g_tls_module_count < TLS_MAX_MODULES) {
        g_tls_modules[g_tls_module_count].rel_tp = rel;
        g_tls_modules[g_tls_module_count].memsz = memsz;
        g_tls_modules[g_tls_module_count].filesz = filesz;
        g_tls_modules[g_tls_module_count].align = align;
        g_tls_modules[g_tls_module_count].init_off = init_addr;
        ++g_tls_module_count;
    }
    *rel_out = rel;
}

/* Returns 0 if there is no TLS anywhere. */
static int tls_setup(uint64_t tp, uint64_t pool_base)
{
    (void)pool_base;
    /* copy init images into the pool; zero the tail (tbss) */
    for (int i = 0; i < g_tls_module_count; ++i) {
        const tls_module_t *m = &g_tls_modules[i];
        uint8_t *dst = (uint8_t *)(uintptr_t)(tp + (uint64_t)m->rel_tp);
        if (m->filesz > 0 && m->init_off != 0) {
            my_memcpy(dst, (const void *)(uintptr_t)m->init_off,
                      m->filesz);
        }
        if (m->memsz > m->filesz) {
            my_memset(dst + m->filesz, 0, m->memsz - m->filesz);
        }
    }
    /* TCB self pointer: [TP] == TP */
    *(uint64_t *)(uintptr_t)tp = tp;
    return 0;
}

static uint64_t ldso_tls_get_addr_impl(ldso_tls_index *ti)
{
    uint64_t tp = g_tls_tp;
    uint64_t module = (ti != NULL) ? ti->ti_module : 0u;
    uint64_t offset = (ti != NULL) ? ti->ti_offset : 0u;
    if (module == 0u || module > (uint64_t)g_tls_module_count) {
        return tp + offset;
    }
    const tls_module_t *m = &g_tls_modules[module - 1u];
    return tp + (uint64_t)m->rel_tp + offset;
}

/* ------------------------------------------------------------------ */
/* Relocations                                                         */
/* ------------------------------------------------------------------ */

extern uint64_t _ldso_tlsdesc_return(void);

static void apply_rela(const dso_t *d, const Elf64_Rela *rela, uint64_t count)
{
    for (uint64_t i = 0; i < count; ++i) {
        const Elf64_Rela *r = &rela[i];
        uint32_t type = ELF64_R_TYPE(r->r_info);
        uint32_t symidx = ELF64_R_SYM(r->r_info);
        uint64_t where = d->bias + r->r_offset;

        switch (type) {
            case R_X86_64_NONE:
                break;
            case R_X86_64_RELATIVE:
                *(uint64_t *)(uintptr_t)where = d->bias + (uint64_t)r->r_addend;
                break;
            case R_X86_64_GLOB_DAT:
                *(uint64_t *)(uintptr_t)where = sym_value(d, symidx);
                break;
            case R_X86_64_64:
                *(uint64_t *)(uintptr_t)where =
                    sym_value(d, symidx) + (uint64_t)r->r_addend;
                break;
            case R_X86_64_PC32:
            case R_X86_64_PLT32: {
                uint64_t s = sym_value(d, symidx);
                *(uint32_t *)(uintptr_t)where =
                    (uint32_t)(s + (uint64_t)r->r_addend - where);
                break;
            }
            case R_X86_64_32:
            case R_X86_64_32S:
                *(uint32_t *)(uintptr_t)where =
                    (uint32_t)(sym_value(d, symidx) + (uint64_t)r->r_addend);
                break;
            case R_X86_64_COPY: {
                const Elf64_Sym *symtab =
                    (const Elf64_Sym *)(uintptr_t)dso_symtab(d);
                if (symidx < d->nsyms) {
                    const Elf64_Sym *sym = &symtab[symidx];
                    uint64_t dst = d->bias + sym->st_value;
                    uint64_t size = sym->st_size;
                    const dso_t *owner = NULL;
                    const Elf64_Sym *def = NULL;
                    if (sym->st_name != 0) {
                        const char *name =
                            (const char *)(uintptr_t)dso_strtab(d) +
                            sym->st_name;
                        /* find the source definition among the DSOs
                           (skip the main executable's copy itself) */
                        for (int k = 1; k < g_ndsos; ++k) {
                            const dso_t *cand = &g_dsos[k];
                            const Elf64_Sym *st =
                                (const Elf64_Sym *)(uintptr_t)dso_symtab(cand);
                            const char *sstr =
                                (const char *)(uintptr_t)dso_strtab(cand);
                            uint64_t n = cand->nsyms;
                            for (uint64_t j = 0; j < n; ++j) {
                                const Elf64_Sym *s2 = &st[j];
                                if (s2->st_name == 0 ||
                                    s2->st_shndx == SHN_UNDEF) {
                                    continue;
                                }
                                if (my_strcmp(name, sstr + s2->st_name) == 0) {
                                    owner = cand;
                                    def = s2;
                                    break;
                                }
                            }
                            if (def != NULL) {
                                break;
                            }
                        }
                    }
                    if (def != NULL && owner != NULL && size != 0) {
                        uint64_t src = owner->bias + def->st_value;
                        if (def->st_size < size) {
                            size = def->st_size;
                        }
                        my_memcpy((void *)(uintptr_t)dst,
                                  (const void *)(uintptr_t)src, size);
                    } else {
                        dbg("[ldso] COPY: cannot find source for symbol\n");
                    }
                }
                break;
            }
            case R_X86_64_DTPMOD64: {
                const dso_t *owner = NULL;
                const Elf64_Sym *def = resolve_def(d, symidx, &owner);
                uint64_t mod = d->modid;
                if (symidx != 0 && def != NULL && owner != NULL &&
                    owner->modid != 0) {
                    mod = owner->modid;
                }
                *(uint64_t *)(uintptr_t)where = mod;
                break;
            }
            case R_X86_64_DTPOFF64:
            case R_X86_64_DTPOFF32: {
                const dso_t *owner = NULL;
                const Elf64_Sym *def = resolve_def(d, symidx, &owner);
                uint64_t off = (def != NULL) ? def->st_value : 0u;
                uint64_t value = off + (uint64_t)r->r_addend;
                if (type == R_X86_64_DTPOFF32) {
                    *(uint32_t *)(uintptr_t)where = (uint32_t)value;
                } else {
                    *(uint64_t *)(uintptr_t)where = value;
                }
                break;
            }
            case R_X86_64_TPOFF64:
            case R_X86_64_TPOFF32:
            case R_X86_64_GOTTPOFF: {
                const dso_t *owner = NULL;
                const Elf64_Sym *def = resolve_def(d, symidx, &owner);
                int64_t rel = d->tls_rel_tp;
                uint64_t off = 0u;
                if (def != NULL && owner != NULL) {
                    rel = owner->tls_rel_tp;
                    off = def->st_value;
                }
                uint64_t value = (uint64_t)rel + off + (uint64_t)r->r_addend;
                if (type == R_X86_64_TPOFF64) {
                    *(uint64_t *)(uintptr_t)where = value;
                } else {
                    *(uint32_t *)(uintptr_t)where = (uint32_t)value;
                }
                break;
            }
            case R_X86_64_TLSDESC: {
                /* The GOT slot holds a descriptor {func, arg}.  This
                   toolchain emits `call *(%rax)` followed by
                   `add %fs:0x0,%rax`, so desc[1] must be the TP-relative
                   offset (same semantics as TPOFF64). */
                const dso_t *owner = NULL;
                const Elf64_Sym *def = resolve_def(d, symidx, &owner);
                int64_t rel = d->tls_rel_tp;
                uint64_t off = 0u;
                if (def != NULL && owner != NULL) {
                    rel = owner->tls_rel_tp;
                    off = def->st_value;
                }
                uint64_t value = (uint64_t)rel + off + (uint64_t)r->r_addend;
                uint64_t *desc = (uint64_t *)(uintptr_t)where;
                desc[0] = (uint64_t)_ldso_tlsdesc_return;
                desc[1] = value;
                break;
            }
            default:
                /* GOTPCREL / GOT32 / 64_32 / 64_32S / IRELATIVE /
                   TLSDESC_CALL etc. do not survive dynamic linking for
                   our toolchain (TLSDESC_CALL needs no action). */
                break;
        }
    }
}

static void apply_relocations(const dso_t *d)
{
    if (d->reladynsz != 0 && d->reladyn != 0) {
        apply_rela(d, (const Elf64_Rela *)(uintptr_t)d->reladyn,
                   d->reladynsz / 24u);
    }
}

/* ------------------------------------------------------------------ */
/* PLT / lazy binding                                                  */
/* ------------------------------------------------------------------ */

extern uint64_t _ldso_plt_resolver(void);

static uint64_t jmprel_count(const dso_t *d)
{
    if (d->pltrelsz == 0 || d->jmprel == 0) {
        return 0;
    }
    uint64_t ent = (d->pltrelent == 17u) ? 16u : 24u;
    if (ent == 0) {
        return 0;
    }
    return d->pltrelsz / ent;
}

static int dso_bind_now(const dso_t *d)
{
    return ((d->flags & DF_BIND_NOW) != 0) ||
           ((d->flags & DF_1_NOW) != 0);
}

static void bind_jmprel_eager(const dso_t *d)
{
    uint64_t count = jmprel_count(d);
    if (count == 0) {
        return;
    }
    uint64_t ent = (d->pltrelent == 17u) ? 16u : 24u;
    const uint8_t *base = (const uint8_t *)(uintptr_t)d->jmprel;
    for (uint64_t i = 0; i < count; ++i) {
        const Elf64_Rela *r = (const Elf64_Rela *)(base + i * ent);
        uint32_t type = ELF64_R_TYPE(r->r_info);
        if (type != R_X86_64_JUMP_SLOT) {
            continue;
        }
        *(uint64_t *)(uintptr_t)(d->bias + r->r_offset) =
            sym_value(d, ELF64_R_SYM(r->r_info));
    }
}

/* Set up a lazy PLT: PLT0 pulls the link_map from GOT+8 and jumps to
   the resolver at GOT+16; each PLT slot's GOT entry starts at
   PLT_slot+6 so the first call traps into the resolver. */
static void setup_plt(dso_t *d)
{
    uint64_t count = jmprel_count(d);
    if (count == 0 || d->pltgot == 0) {
        return;
    }
    if (dso_bind_now(d)) {
        bind_jmprel_eager(d);
        return;
    }
    uint64_t ent = (d->pltrelent == 17u) ? 16u : 24u;
    const uint8_t *base = (const uint8_t *)(uintptr_t)d->jmprel;
    uint64_t got = dso_vaddr(d, d->pltgot);

    /* The linker initializes the first JUMP_SLOT GOT entry to
       (first PLT slot address) + 6, bias-relative for shared objects. */
    const Elf64_Rela *r0 = (const Elf64_Rela *)base;
    uint64_t slot1_rel = *(uint64_t *)(uintptr_t)(d->bias + r0->r_offset);
    if (slot1_rel < 0x1000u || slot1_rel > 0x80000000u) {
        /* Not the expected PLT-pointer pattern (e.g. -z now without
           flags, or REL relocs): fall back to eager binding. */
        bind_jmprel_eager(d);
        return;
    }
    uint64_t plt_base_rel = slot1_rel - 6u - 16u;

    *(uint64_t *)(uintptr_t)(got + 8u) = (uint64_t)d;
    *(uint64_t *)(uintptr_t)(got + 16u) = (uint64_t)_ldso_plt_resolver;
    for (uint64_t i = 0; i < count; ++i) {
        const Elf64_Rela *r = (const Elf64_Rela *)(base + i * ent);
        if (ELF64_R_TYPE(r->r_info) != R_X86_64_JUMP_SLOT) {
            continue;
        }
        uint64_t slot = d->bias + plt_base_rel + 16u * (i + 1u) + 6u;
        *(uint64_t *)(uintptr_t)(d->bias + r->r_offset) = slot;
    }
}

/* Resolver for lazy PLT entries, called from _ldso_plt_resolver. */
uint64_t ldso_fixup(uint64_t link_map, uint64_t reloc_index)
{
    dso_t *d = (dso_t *)(uintptr_t)link_map;
    uint64_t count = jmprel_count(d);
    if (count == 0 || reloc_index >= count) {
        dbg("[ldso] lazy fixup out of range\n");
        return 0;
    }
    uint64_t ent = (d->pltrelent == 17u) ? 16u : 24u;
    const Elf64_Rela *r =
        (const Elf64_Rela *)((const uint8_t *)(uintptr_t)d->jmprel +
                             reloc_index * ent);
    uint32_t symidx = ELF64_R_SYM(r->r_info);
    uint32_t type = ELF64_R_TYPE(r->r_info);
    uint64_t value = sym_value(d, symidx);
    if (d->pltrelent == 17u) {
        *(uint32_t *)(uintptr_t)(d->bias + r->r_offset) = (uint32_t)value;
    } else {
        *(uint64_t *)(uintptr_t)(d->bias + r->r_offset) = value;
    }
    if (type == R_X86_64_JUMP_SLOT && symidx < d->nsyms) {
        const Elf64_Sym *st =
            (const Elf64_Sym *)(uintptr_t)dso_symtab(d);
        const char *sstr = (const char *)(uintptr_t)dso_strtab(d);
        const char *nm = (st[symidx].st_name != 0) ?
            sstr + st[symidx].st_name : "?";
        dbg("[ldso] lazy ");
        if (d->soname != NULL) {
            dbg(d->soname);
            dbg(": ");
        } else {
            dbg("main: ");
        }
        dbg(nm);
        dbg(" -> ");
        dbg_hex(value);
        dbg("\n");
    }
    (void)type;
    return value;
}

/* ------------------------------------------------------------------ */
/* DT_NEEDED loading (breadth-first, dedup by soname)                  */
/* ------------------------------------------------------------------ */

#define MAX_LIB_DIRS 4
#define MAX_PRELOADS 4

static char g_lib_dirs[MAX_LIB_DIRS][160];
static int g_n_lib_dirs = 0;
static char g_preloads[MAX_PRELOADS][96];
static int g_n_preloads = 0;

static int my_strcmp_n(const char *a, const char *b, uint64_t n)
{
    for (uint64_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return (int)((uint8_t)a[i] - (uint8_t)b[i]);
        }
        if (a[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

static void parse_env(const uint64_t *envp)
{
    for (uint64_t i = 0; envp[i] != 0; ++i) {
        const char *e = (const char *)(uintptr_t)envp[i];
        if (my_strcmp_n(e, "LD_LIBRARY_PATH=", 16u) == 0) {
            const char *v = e + 16u;
            while (*v != '\0' && g_n_lib_dirs < MAX_LIB_DIRS) {
                const char *end = v;
                while (*end != '\0' && *end != ':') {
                    ++end;
                }
                uint64_t l = (uint64_t)(end - v);
                if (l > 0 && l + 1u < sizeof(g_lib_dirs[0])) {
                    my_memcpy(g_lib_dirs[g_n_lib_dirs], v, l);
                    g_lib_dirs[g_n_lib_dirs][l] = '\0';
                    ++g_n_lib_dirs;
                }
                v = (*end == ':') ? end + 1 : end;
            }
        } else if (my_strcmp_n(e, "LD_PRELOAD=", 11u) == 0) {
            const char *v = e + 11u;
            while (*v != '\0' && g_n_preloads < MAX_PRELOADS) {
                const char *end = v;
                while (*end != '\0' && *end != ' ' && *end != ':') {
                    ++end;
                }
                uint64_t l = (uint64_t)(end - v);
                if (l > 0 && l + 1u < sizeof(g_preloads[0])) {
                    my_memcpy(g_preloads[g_n_preloads], v, l);
                    g_preloads[g_n_preloads][l] = '\0';
                    ++g_n_preloads;
                }
                v = (*end == ' ' || *end == ':') ? end + 1 : end;
            }
        }
    }
}

static int dso_already_loaded(const char *name)
{
    for (int i = 1; i < g_ndsos; ++i) {
        if (g_dsos[i].soname != NULL &&
            my_strcmp(g_dsos[i].soname, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int find_path_for(const char *name, char *path, uint64_t cap)
{
    if (name[0] == '/') {
        uint64_t l = my_strlen(name);
        if (l + 1u > cap) {
            return -1;
        }
        my_memcpy(path, name, l + 1u);
        return 0;
    }

    uint64_t l = my_strlen(name);
    char cand[256];
    int64_t fd = -1;

    /* 1) LD_LIBRARY_PATH directories (highest priority) */
    for (int k = 0; k < g_n_lib_dirs; ++k) {
        uint64_t dl = my_strlen(g_lib_dirs[k]);
        if (dl + 1u + l + 1u > sizeof(cand)) {
            continue;
        }
        my_memcpy(cand, g_lib_dirs[k], dl);
        cand[dl] = '/';
        my_memcpy(cand + dl + 1u, name, l + 1u);
        fd = sc_open(cand);
        if (fd >= 0) {
            (void)sc_close(fd);
            if (dl + 1u + l + 1u <= cap) {
                my_memcpy(path, cand, dl + 1u + l + 1u);
            } else {
                my_memcpy(path, name, l + 1u);
            }
            return 0;
        }
    }

    /* 2) the executable's directory */
    if (g_execfn_dir[0] != '\0' && l + my_strlen(g_execfn_dir) + 2u <= cap) {
        uint64_t dl = my_strlen(g_execfn_dir);
        my_memcpy(path, g_execfn_dir, dl);
        path[dl] = '/';
        my_memcpy(path + dl + 1u, name, l + 1u);
        return 0;
    }

    /* 3) /Userland/Service/<name-without-.so>/<name> -- Userland services
     * each live in their own directory named after the soname stem. */
    {
        uint64_t stem = l;
        if (stem > 3u && name[stem - 3u] == '.' && name[stem - 2u] == 's' &&
            name[stem - 1u] == 'o') {
            stem -= 3u;
        }
        const char *pfx = "/Userland/Service/";
        uint64_t pl = 17u;
        if (pl + stem + 1u + l + 1u <= sizeof(cand)) {
            my_memcpy(cand, pfx, pl);
            my_memcpy(cand + pl, name, stem);
            cand[pl + stem] = '/';
            my_memcpy(cand + pl + stem + 1u, name, l + 1u);
            fd = sc_open(cand);
            if (fd >= 0) {
                (void)sc_close(fd);
                uint64_t total = pl + stem + 1u + l + 1u;
                if (total <= cap) {
                    my_memcpy(path, cand, total);
                    return 0;
                }
            }
        }
    }

    /* 4) /Userland/Library 5) /lib */
    if (l + 19u <= cap) {
        my_memcpy(path, "/Userland/Library/", 18u);
        my_memcpy(path + 18u, name, l + 1u);
        return 0;
    }
    if (l + 5u <= cap) {
        my_memcpy(path, "/lib/", 5u);
        my_memcpy(path + 5u, name, l + 1u);
        return 0;
    }
    return -1;
}

static void load_preloads(void)
{
    for (int k = 0; k < g_n_preloads; ++k) {
        const char *name = g_preloads[k];
        if (dso_already_loaded(name)) {
            continue;
        }
        char path[256];
        if (find_path_for(name, path, sizeof(path)) != 0) {
            dbg("[ldso] LD_PRELOAD not found: ");
            dbg(name);
            dbg("\n");
            sc_exit_group(127);
        }
        dbg("[ldso] preload: ");
        dbg(name);
        dbg("\n");
        if (g_ndsos >= MAX_DSOS) {
            sc_exit_group(127);
        }
        dso_t *out = &g_dsos[g_ndsos];
        my_memset(out, 0, sizeof(*out));
        int rc = load_elf_file(path, out);
        if (rc != 0) {
            dbg("[ldso] failed to load preload: ");
            dbg(name);
            dbg("\n");
            sc_exit_group(127);
        }
        out->soname = name;
        ++g_ndsos;
    }
}

static void load_dso_chain(void)
{
    int head = 0;

    while (head < g_ndsos) {
        const dso_t *cur = &g_dsos[head];
        ++head;

        const char *strtab = (const char *)(uintptr_t)dso_strtab(cur);
        const Elf64_Dyn *dyn = (const Elf64_Dyn *)(uintptr_t)cur->dynamic;
        while (dyn->d_tag != DT_NULL) {
            if (dyn->d_tag == DT_NEEDED) {
                const char *name = strtab + dyn->d_val;
                if (dso_already_loaded(name)) {
                    ++dyn;
                    continue;
                }
                char path[256];
                if (find_path_for(name, path, sizeof(path)) != 0) {
                    dbg("[ldso] no search path for ");
                    dbg(name);
                    dbg("\n");
                    sc_exit_group(127);
                }

                dbg("[ldso] needed: ");
                dbg(name);
                dbg("\n");

                if (g_ndsos >= MAX_DSOS) {
                    sc_exit_group(127);
                }
                dso_t *out = &g_dsos[g_ndsos];
                my_memset(out, 0, sizeof(*out));
                int rc = load_elf_file(path, out);
                if (rc != 0) {
                    dbg("[ldso] failed to load library: ");
                    dbg(name);
                    dbg("\n");
                    sc_exit_group(127);
                }
                out->soname = name;
                dbg("[ldso] loaded ");
                dbg(name);
                dbg(" bias=");
                dbg_hex(out->bias);
                dbg("\n");
                ++g_ndsos;
            }
            ++dyn;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

void ldso_run(uint64_t *stack)
{
    g_initial_rsp = (uint64_t)stack;

    uint64_t argc = stack[0];
    const uint64_t *argv = stack + 1;
    (void)argc;

    const uint64_t *envp = argv + argc + 1;
    parse_env(envp);
    while (*envp != 0) {
        ++envp;
    }
    ++envp;

    uint64_t at_phdr = 0;
    uint64_t at_phnum = 0;
    uint64_t at_entry = 0;
    uint64_t at_execfn = 0;

    const uint64_t *auxv = envp;
    while (auxv[0] != 0) {
        switch (auxv[0]) {
            case 3:  /* AT_PHDR */
                at_phdr = auxv[1];
                break;
            case 4:  /* AT_PHENT */
                break;
            case 5:  /* AT_PHNUM */
                at_phnum = auxv[1];
                break;
            case 9:  /* AT_ENTRY */
                at_entry = auxv[1];
                break;
            case 31: /* AT_EXECFN */
                at_execfn = auxv[1];
                break;
            default:
                break;
        }
        auxv += 2;
    }

    dbg("[ldso] starting\n");
    if (at_phdr == 0 || at_phnum == 0 || at_entry == 0) {
        dbg("[ldso] missing auxv\n");
        sc_exit_group(127);
    }

    /* record execfn directory for library search */
    g_execfn_dir[0] = '\0';
    if (at_execfn != 0) {
        const char *exe = (const char *)(uintptr_t)at_execfn;
        uint64_t l = my_strlen(exe);
        uint64_t slash = 0;
        for (uint64_t i = 0; i < l; ++i) {
            if (exe[i] == '/') {
                slash = i;
            }
        }
        if (slash > 0) {
            my_memcpy(g_execfn_dir, exe, slash);
            g_execfn_dir[slash] = '\0';
        }
    }

    /* main executable: bias 0, dynamic from its phdrs */
    dso_t *main_dso = &g_dsos[0];
    my_memset(main_dso, 0, sizeof(*main_dso));

    const Elf64_Phdr *ph = (const Elf64_Phdr *)(uintptr_t)at_phdr;
    uint64_t dyn_vaddr = 0;
    for (uint64_t i = 0; i < at_phnum; ++i) {
        if (ph[i].p_type == PT_DYNAMIC) {
            dyn_vaddr = ph[i].p_vaddr;
            break;
        }
    }
    if (dyn_vaddr == 0) {
        dbg("[ldso] no PT_DYNAMIC in main\n");
        sc_exit_group(127);
    }
    main_dso->bias = 0;
    main_dso->dynamic = dyn_vaddr;
    main_dso->sysv_hash = 0;
    main_dso->gnu_hash = 0;
    main_dso->tls_align = 16;

    /* capture the main executable's TLS segment */
    for (uint64_t i = 0; i < at_phnum; ++i) {
        if (ph[i].p_type == PT_TLS) {
            main_dso->tls_memsz = ph[i].p_memsz;
            main_dso->tls_filesz = ph[i].p_filesz;
            main_dso->tls_align = ph[i].p_align;
            main_dso->tls_init = ph[i].p_vaddr;
            g_tls_main_has_tls = 1;
            break;
        }
    }

    const Elf64_Dyn *dyn = (const Elf64_Dyn *)(uintptr_t)dyn_vaddr;
    while (dyn->d_tag != DT_NULL) {
        switch (dyn->d_tag) {
            case DT_STRTAB:
                main_dso->strtab = dyn->d_val;
                break;
            case DT_SYMTAB:
                main_dso->symtab = dyn->d_val;
                break;
            case DT_HASH:
                main_dso->sysv_hash = dyn->d_val;
                break;
            case DT_GNU_HASH:
                main_dso->gnu_hash = dyn->d_val;
                break;
            case DT_RELA:
                main_dso->reladyn = dyn->d_val;
                break;
            case DT_RELASZ:
                main_dso->reladynsz = dyn->d_val;
                break;
            case DT_JMPREL:
                main_dso->jmprel = dyn->d_val;
                break;
            case DT_PLTRELSZ:
                main_dso->pltrelsz = dyn->d_val;
                break;
            case DT_PLTREL:
                main_dso->pltrelent = dyn->d_val;
                break;
            case DT_PLTGOT:
                main_dso->pltgot = dyn->d_val;
                break;
            case DT_FLAGS:
                main_dso->flags |= dyn->d_val;
                break;
            case DT_FLAGS_1:
                main_dso->flags |= dyn->d_val;
                break;
            case DT_INIT:
                main_dso->init = dyn->d_val;
                break;
            case DT_INIT_ARRAY:
                main_dso->init_arr = dyn->d_val;
                break;
            case DT_INIT_ARRAYSZ:
                main_dso->init_arrsz = dyn->d_val;
                break;
            default:
                break;
        }
        ++dyn;
    }
    if (main_dso->strtab == 0 || main_dso->symtab == 0) {
        dbg("[ldso] main has no dynsym\n");
        sc_exit_group(127);
    }
    if (main_dso->sysv_hash != 0) {
        const uint32_t *h = (const uint32_t *)(uintptr_t)main_dso->sysv_hash;
        main_dso->nsyms = h[1];
    } else if (main_dso->gnu_hash != 0) {
        const uint32_t *h = (const uint32_t *)(uintptr_t)main_dso->gnu_hash;
        uint32_t nbuckets = h[0];
        uint32_t symoffset = h[1];
        uint32_t nchain = 0;
        const uint32_t *buckets = h + 4 + (h[2] * sizeof(uint64_t)) / 4u;
        const uint32_t *chain = buckets + nbuckets;
        for (uint32_t b = 0; b < nbuckets; ++b) {
            uint32_t idx = buckets[b];
            if (idx < symoffset) {
                continue;
            }
            while ((chain[idx - symoffset] & 1u) == 0) {
                ++idx;
            }
            if (idx + 1u > nchain) {
                nchain = idx + 1u;
            }
        }
        main_dso->nsyms = nchain;
    }
    if (main_dso->nsyms == 0) {
        dbg("[ldso] main has no symbols\n");
        sc_exit_group(127);
    }
    g_ndsos = 1;

    dbg("[ldso] main entry=");
    dbg_hex(at_entry);
    dbg(" dynamic=");
    dbg_hex(dyn_vaddr);
    dbg("\n");

    /* LD_PRELOAD libraries are loaded before the DT_NEEDED chain so
       their symbols interpose (they sit between main and the deps in
       the global scope). */
    load_preloads();

    /* recursively load all DT_NEEDED chains */
    load_dso_chain();

    /* register TLS modules: main first, then DSOs in load order */
    if (g_tls_main_has_tls) {
        int64_t rel = 0;
        uint64_t align = main_dso->tls_align;
        if (align < 16u) {
            align = 16u;
        }
        rel = -(int64_t)align_up(main_dso->tls_memsz, align);
        g_tls_cursor = (uint64_t)(-rel);
        g_tls_modules[0].rel_tp = rel;
        g_tls_modules[0].memsz = main_dso->tls_memsz;
        g_tls_modules[0].filesz = main_dso->tls_filesz;
        g_tls_modules[0].align = align;
        g_tls_modules[0].init_off = main_dso->tls_init;
        g_tls_module_count = 1;
        main_dso->modid = 1;
    } else {
        g_tls_cursor = 0;
        g_tls_module_count = 0;
        main_dso->modid = 0;
    }
    for (int i = 1; i < g_ndsos; ++i) {
        dso_t *d = &g_dsos[i];
        if (d->tls_memsz == 0) {
            continue;
        }
        int64_t rel = 0;
        tls_register(d->bias + d->tls_init, d->tls_memsz, d->tls_filesz,
                     d->tls_align, &rel);
        d->tls_rel_tp = rel;
        d->modid = (uint64_t)g_tls_module_count;
    }

    /* allocate the static TLS pool and point FS at it */
    if (g_tls_module_count > 0) {
        uint64_t max_align = 16u;
        for (int i = 0; i < g_tls_module_count; ++i) {
            if (g_tls_modules[i].align > max_align) {
                max_align = g_tls_modules[i].align;
            }
        }
        uint64_t total = tls_pool_total(g_tls_cursor) + max_align;
        uint8_t *pool = (uint8_t *)sc_mmap(total);
        if (pool == (uint8_t *)0 || (uintptr_t)pool == ~0ULL) {
            dbg("[ldso] TLS pool mmap failed\n");
            sc_exit_group(127);
        }
        uint64_t tp = ((uint64_t)pool + total) & ~(max_align - 1u);
        g_tls_tp = tp;
        tls_setup(tp, (uint64_t)pool);
        sc_arch_set_fs(tp);
    }

    /* apply relocations: DSOs first (reverse load order), then main */
    for (int i = g_ndsos - 1; i > 0; --i) {
        apply_relocations(&g_dsos[i]);
    }
    apply_relocations(main_dso);

    /* PLT setup: lazy by default; -z now objects bind eagerly */
    for (int i = g_ndsos - 1; i >= 0; --i) {
        setup_plt(&g_dsos[i]);
    }

    /* run initializers: dependencies first, then main */
    for (int i = g_ndsos - 1; i >= 0; --i) {
        const dso_t *d = &g_dsos[i];
        if (d->init != 0) {
            ((void (*)(void))(uintptr_t)d->init)();
        }
        if (d->init_arrsz != 0 && d->init_arr != 0) {
            uint64_t count = d->init_arrsz / 8u;
            void (**ctors)(void) =
                (void (**)(void))(uintptr_t)d->init_arr;
            for (uint64_t k = 0; k < count; ++k) {
                if (ctors[k] != NULL) {
                    ctors[k]();
                }
            }
        }
    }

    dbg("[ldso] jumping to main entry\n");
    ldso_jump(at_entry, g_initial_rsp);
}
