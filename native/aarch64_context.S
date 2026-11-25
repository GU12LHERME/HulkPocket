/* Experimental AArch64 cooperative context switch for HulkPocket.
 * AAPCS64 callee-saved state: x19-x29, x30 (link register), SP, d8-d15.
 * Not integrated with XboxXBE-Recomp fibers or game yet.
 */
.text
.p2align 2
.global hulkpocket_aarch64_switch
.type hulkpocket_aarch64_switch, %function
hulkpocket_aarch64_switch:
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x9, sp
    str x9, [x0, #96]
    stp d8, d9, [x0, #104]
    stp d10, d11, [x0, #120]
    stp d12, d13, [x0, #136]
    stp d14, d15, [x0, #152]
    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldp x29, x30, [x1, #80]
    ldr x9, [x1, #96]
    mov sp, x9
    ldp d8, d9, [x1, #104]
    ldp d10, d11, [x1, #120]
    ldp d12, d13, [x1, #136]
    ldp d14, d15, [x1, #152]
    ret
.size hulkpocket_aarch64_switch, . - hulkpocket_aarch64_switch
