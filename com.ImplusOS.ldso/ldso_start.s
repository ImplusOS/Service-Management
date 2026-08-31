    extern ldso_run
    extern ldso_jump
    extern ldso_fixup

    global _start
    global syscall3
    global syscall6
    global _ldso_plt_resolver
    global _ldso_tlsdesc_return

    section .text

_start:
    mov    rdi, rsp
    call   ldso_run
.loop:
    hlt
    jmp    .loop

; int64_t syscall3(uint64_t nr, uint64_t a, uint64_t b, uint64_t c)
syscall3:
    mov    rax, rdi
    mov    rdi, rsi
    mov    rsi, rdx
    mov    rdx, rcx
    syscall
    ret

; int64_t syscall6(uint64_t nr, uint64_t a, uint64_t b, uint64_t c,
;                  uint64_t d, uint64_t e, uint64_t f)
; SysV C ABI:  a=rdi b=rsi c=rdx d=rcx e=r8 f=r9 (7th arg on stack)
; syscall ABI: a=rdi b=rsi c=rdx d=r10 e=r8 f=r9
syscall6:
    mov    r10, r8
    mov    r8,  r9
    mov    r9,  [rsp+8]
    mov    rax, rdi
    mov    rdi, rsi
    mov    rsi, rdx
    mov    rdx, rcx
    syscall
    ret

; void ldso_jump(uint64_t entry, uint64_t rsp)
ldso_jump:
    mov    rax, rdi
    mov    rsp, rsi
    jmp    rax

; Lazy PLT binding trampoline.  Invoked by a PLT slot through PLT0:
;   PLT slot: jmp [GOT[n]]; push <reloc index>; jmp PLT0
;   PLT0:     push [GOT+8]; jmp [GOT+16]
; On entry the stack is: [link_map] [reloc_index] [caller retaddr] <args>
; (rsp is 8 mod 16, matching the ABI state the caller's call produced).
_ldso_plt_resolver:
    push   rbp
    mov    rbp, rsp
    push   r11
    push   r10
    push   r9
    push   r8
    push   rsi
    push   rdi
    push   rcx
    push   rdx
    mov    rdi, [rbp+8]          ; link_map (dso_t *)
    mov    rsi, [rbp+16]         ; PLT reloc index
    call   ldso_fixup           ; returns target address in rax
    pop    rdx
    pop    rcx
    pop    rdi
    pop    rsi
    pop    r8
    pop    r9
    pop    r10
    pop    r11
    pop    rbp
    add    rsp, 16              ; drop link_map + reloc index
    jmp    rax

; TLSDESC resolver: desc[0] = this function, desc[1] = TLS address.
; Called with rax = &descriptor; must return the address in rax.
_ldso_tlsdesc_return:
    mov    rax, [rax+8]
    ret
