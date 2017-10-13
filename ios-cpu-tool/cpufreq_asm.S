//Win CC
//Int        : rcx, rdx, r8, r9. Others passed on stack
//Fp         : xmm0 - xmm3. Others passed on stack
//Volatile   : rax,rcx,rdx,r8,r9, r10,r11, xmm0-xmm5 
//CallerSave : r10,r11,xmm4-xmm5
//
//AMD CC
//rdi - rsi - rdx - rcx - r8 - r9

.macro ALIGN_FUNC
     .align 4
.endm
.macro ALIGN_LOOP
     .align 4
.endm
.macro ALIGN_CACHELINE
     .align 6
.endm

.macro DEFUN name
     ALIGN_FUNC
     .global \name
\name:
.endm

.macro DEFUNT name
ALIGN_FUNC
.global \name
.thumb_func \name
.syntax   unified
.code     16
\name:
.endm


.equ N, (80 * 1000 * 100)


#ifdef __arm__

DEFUNT thumb_entry

bx lr

#elif __arm64__

DEFUN _dummy_call
    ret

DEFUN _calib_seq_add

1:  .rept 20
    add x1, x1, #1
    .endr
    subs x0, x0, #1
    bne 1b
    ret

#else  //x64

DEFUN _dummy_call
    retq

DEFUN _calibration

    push %rax
    movq %rsp, (%rsp)
    movq %rsp, %rcx

    movl $N, %eax
    ALIGN_LOOP
1:  .rept 20
    mov (%ecx), %ecx
    .endr
    sub $1, %rdi
    jne 1b
    popq %rax
    retq

DEFUN _calib_seq_add

1:  .rept 20
    addl %ecx, %ecx
    .endr
    sub $1, %rdi
    jne 1b
    retq

DEFUN _calib_seq_nop

1:  .rept 20
    nop
    .endr
    sub $1, %rdi
    jne 1b
    retq

#endif




