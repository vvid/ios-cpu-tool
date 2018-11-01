//Win CC
//Int        : rcx, rdx, r8, r9. Others passed on stack
//Fp         : xmm0 - xmm3. Others passed on stack
//Volatile   : rax,rcx,rdx,r8,r9, r10,r11, xmm0-xmm5 
//CallerSave : r10,r11,xmm4-xmm5
//
//AMD CC
//rdi - rsi - rdx - rcx - r8 - r9

//ARM64
//r0-7 input/res
//r8   indirect result location
//r9-r15 tmp
//r16-r18 IPx
//r19-r28 callee-saved
//r29-30 FP LR

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

    .equ N, (8 * 1000 * 1000)


#ifdef __arm__

DEFUN _dummy_call
    bx      lr

DEFUNT _calib_seq_add

1:  .rept 20
    add     r1, r1, #1
    .endr
    subs    r0, r0, #20
    bne     1b
    bx      lr

DEFUN _calib_signature
    bx      lr

DEFUN _sync_threads
    bx      lr

#elif __arm64__

DEFUN _read_raw_timebase
    mrs     x0, CNTFRQ_EL0
    ret

DEFUN _read_raw_timer
    isb
    mrs     x0, CNTPCT_EL0
    ret

.macro BEG_TIMER reg
    isb
    mrs     \reg, CNTPCT_EL0
.endm

.macro END_TIMER reg
    isb
    mrs     x0, CNTPCT_EL0
    sub     x0, x0, \reg
.endm

DEFUN _dummy_call
    ret

DEFUN _calib_seq_add
    BEG_TIMER x1
    ALIGN_LOOP
1:  .rept   19
    sub     x0, x0, #1
    .endr
    subs    x0, x0, #1
    bne     1b
    END_TIMER x1
    ret

DEFUN _calib_seq_add10
    BEG_TIMER x1
    ALIGN_LOOP
1:  .rept   9
    sub     x0, x0, #1
    .endr
    subs    x0, x0, #1
    bne     1b
    END_TIMER x1
    ret

//just something depending on issue width so far
DEFUN _calib_signature
    BEG_TIMER x8
    ALIGN_LOOP
//    veor.i64 v31, v31, v31
1:  .rept   1
    add     x1,x1,x15
    add     x2,x2,x15
    add     x3,x3,x15
    add     x4,x4,x15

    add     x5,x5,x15
    add     x6,x6,x15
    add     x7,x7,x15
    add     x9,x9,x15

    add     x10,x10,x15
    add     x11,x11,x15
    add     x12,x12,x15
    add     x13,x13,x15
#if 0
    vmla.f64 d0,d0,d31
    vmla.f64 d1,d1,d31
    vmla.f64 d2,d2,d31
    vmla.f64 d3,d3,d31

    vmla.f64 v4,v4,v31
    vmla.f64 v5,v5,v31
    vmla.f64 v6,v6,v31
    vmla.f64 v7,v7,v31
#endif
    add     x1,x1,x15
    add     x2,x2,x15
    add     x3,x3,x15
    add     x4,x4,x15

    add     x5,x5,x15
    add     x6,x6,x15
    add     x7,x7,x15
    add     x9,x9,x15

    add     x10,x10,x15
    add     x11,x11,x15
    add     x12,x12,x15
    add     x13,x13,x15
#if 0
    vmla.f64 v16,v16,v31
    vmla.f64 v17,v17,v31
    vmla.f64 v18,v18,v31
    vmla.f64 v19,v19,v31

    vmla.f64 v20,v20,v31
    vmla.f64 v21,v21,v31
    vmla.f64 v22,v22,v31
    vmla.f64 v23,v23,v31
#endif
    .endr
    subs    x0, x0, #1
    bne     1b
    END_TIMER x8
    ret


// thread_id, num_threads, stop_event.w, atomic_var.w

DEFUN _sync_threads
    sub     x1, x1, #1
    sub     x5, x0, #1
    cbz     x0, 2f       //write 0, start the line up

    //wait thread_id - 1
1:  ldr     w4, [x3]
    ldr     w6, [x2]
    cbnz    w6, 4f
    cmp     w4, w5
    bne     1b

2:  str     w0, [x3]

3:  ldr     w4, [x3]
    ldr     w6, [x2]
    cbnz    w6, 4f
    cmp     w4, w1
    bne     3b

4:  ret


// ptr, count
DEFUN _a_mem_max_loads
 1:
    ldr	    w2,[x0, #0x00]
    ldr	    w3,[x0, #0x04]
    ldr	    w4,[x0, #0x08]
    ldr	    w5,[x0, #0x0c]

    subs    x1, x1, #1

    ldr	    w10,[x0, #0x10]
    ldr	    w11,[x0, #0x14]
    ldr	    w12,[x0, #0x18]
    ldr	    w13,[x0, #0x1c]

    bne     1b
    ret

DEFUN _a_mem_max_stores
 1:
    str	    w2,[x0, #0x00]
    str	    w3,[x0, #0x04]
    str	    w4,[x0, #0x08]
    str	    w5,[x0, #0x0c]

    subs    x1, x1, #1

    str	    w10,[x0, #0x10]
    str	    w11,[x0, #0x14]
    str	    w12,[x0, #0x18]
    str	    w13,[x0, #0x1c]

    bne     1b
    ret

// ptr, count
DEFUN _a_mem_walk
 1:
    ldr	    x0,[x0, #0]
    ldr	    x0,[x0, #0]
    ldr	    x0,[x0, #0]
    ldr	    x0,[x0, #0]
    subs    x1, x1, #1
    ldr	    x0,[x0, #0]
    ldr	    x0,[x0, #0]
    ldr	    x0,[x0, #0]
    ldr	    x0,[x0, #0]
    bne     1b
    ret


//cnt_inner, cnt_outer, ptr, stride
DEFUN _a_mem_fetchline1
    mov	    x10,x0
    mov	    x12,x2
    ALIGN_LOOP
 1:
    ldr	    w4,[x2, #0x00]
    add	    x2,x2,x3
    subs    x0,x0, #1
    bne     1b
    mov	    x2,x12
    mov	    x0,x10
    subs    x1,x1, #1
    bne     1b
    ret

//cnt_inner, cnt_outer, ptr, stride
DEFUN _a_mem_fetchline2
    mov	    x10,x0
    mov	    x12,x2
    ALIGN_LOOP
 1:
    ldr	    w4,[x2, #0x00]
    ldr	    w5,[x2, #0x10]
    add	    x2,x2,x3
    subs    x0,x0, #1
    bne     1b
    mov	    x2,x12
    mov	    x0,x10
    subs    x1,x1, #1
    bne     1b
    ret

#else  //x64

DEFUN _dummy_call
    retq

DEFUN _calib_seq_add_x64

1:  .rept   20
    addl    %ecx, %ecx
    .endr
    sub     $20, %rdi
    jne     1b
    retq

DEFUN _calib_seq_nop

1:  .rept   20
    nop
    .endr
    sub     $1, %rdi
    jne     1b
    retq

DEFUN _calib_signature_x64
    push    %rax
    movq    %rsp, (%rsp)
    movq    %rsp, %rcx

    movl    $N, %eax
    ALIGN_LOOP
1:  .rept   20
    mov     (%rcx), %rcx
    .endr
    sub     $1, %rdi
    jne     1b
    popq    %rax
    retq

DEFUN _read_raw_timebase


DEFUN _read_raw_timer

    retq


DEFUN _a_mem_max_loads
DEFUN _a_mem_max_stores
DEFUN _a_mem_walk
DEFUN _a_mem_fetchline1
DEFUN _a_mem_fetchline2
    retq

DEFUN _sync_threads

    retq

#endif

