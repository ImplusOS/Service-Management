    extern dynmain_main

    global _start
    global syscall3

    section .text

_start:
    mov    rdi, rsp
    call   dynmain_main
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
