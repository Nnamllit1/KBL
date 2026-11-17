/* Stackless control: run C++, then enter the original kernel with x0..x3 intact. */
.section .text.entry_control, "ax"
.global entry_control
entry_control:
    mov x19, x0
    mov x20, x1
    mov x21, x2
    mov x22, x3
    mov x23, x30
    ldr x0, =KBL_BANNER_ADDRESS
    ldr x1, =KBL_PROC_MARKER_ADDRESS
    bl entry_control_cpp
    ldr x9, =KBL_BANNER_ADDRESS
    dc cvac, x9
    ldr x9, =KBL_PROC_MARKER_ADDRESS
    dc cvac, x9
    add x9, x9, #63
    dc cvac, x9
    add x9, x9, #63
    dc cvac, x9
    dsb sy
    isb
    mov x0, x19
    mov x1, x20
    mov x2, x21
    mov x3, x22
    mov x30, x23
    ldr x9, =KBL_KERNEL_ENTRY
    br x9
    .ltorg
