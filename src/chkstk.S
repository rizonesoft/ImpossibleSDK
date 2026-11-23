/* Stack probe for functions with more than a page of locals: touches each page
   in turn so the guard page moves down. Size in rax; every register is kept. */
    .text
    .globl ___chkstk_ms
___chkstk_ms:
    pushq %rcx
    pushq %rax
    leaq 24(%rsp), %rcx
1:  cmpq $0x1000, %rax
    jb 2f
    subq $0x1000, %rcx
    orq $0, (%rcx)
    subq $0x1000, %rax
    jmp 1b
2:  subq %rax, %rcx
    orq $0, (%rcx)
    popq %rax
    popq %rcx
    ret
