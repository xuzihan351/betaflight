/*
 * Copyright (c) 2021-2025 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * RISC-V startup for HPM6750 (Betaflight Make build)
 * Adapted from hpm_sdk/soc/HPM6700/HPM6750/toolchains/gcc/start.S
 *
 * HPM6750: RV32IMAFC, 2MB Flash, 256K ILM, 256K DLM, 512K AXI SRAM0
 */

/* CSR register definitions (from hpm_sdk/soc/HPM6700/HPM6750/hpm_csr_regs.h) */
#define CSR_MSTATUS_FS_MASK     0x00006000
#define CSR_MMISC_CTL           0x7D0

    .section .start, "ax"

    .global _start
    .type _start,@function

_start:
    /* Initialize global pointer */
    .option push
    .option norelax
    la gp, __global_pointer$
    la tp, __thread_pointer$
    .option pop

    /* Reset mstatus to 0 */
    csrrw x0, mstatus, x0

#ifdef __riscv_flen
    /* Enable FPU */
    li t0, CSR_MSTATUS_FS_MASK
    csrrs t0, mstatus, t0

    /* Initialize FCSR */
    fscsr zero
#endif

    /* Enable LMM1 clock (HPM6750-specific) */
    la t0, 0xF4000800
    lw t1, 0(t0)
    ori t1, t1, 0x80
    sw t1, 0(t0)

    /* Initialize stack pointer */
    la t0, _stack
    mv sp, t0

    /* Enable I-cache */
    call l1c_ic_enable

    /* Enable D-cache */
    call l1c_dc_enable
    call l1c_dc_invalidate_all

    /*
     * Initialize LMA/VMA sections.
     * Relocation for any sections that need to be copied from flash to RAM.
     */
    call c_startup

    /* Do global constructors */
    call __libc_init_array

#ifndef NO_CLEANUP_AT_START
    /* Clean up */
    call _clean_up
#endif

    /* Bare-metal: use irq_handler_trap for all exceptions */
#define HANDLER_TRAP irq_handler_trap

#if !defined(USE_NONVECTOR_MODE) || (USE_NONVECTOR_MODE == 0)
    /* Initial machine trap-vector base */
    la t0, __vector_table
    csrw mtvec, t0

    /* Enable vectored external PLIC interrupt */
    csrsi CSR_MMISC_CTL, 2
#else
    /* Non-vectored mode */
    la t0, HANDLER_TRAP
    csrw mtvec, t0
    csrci CSR_MMISC_CTL, 2
#endif

    /* System reset handler -> main() */
    call reset_handler

    /* Infinite loop, if returned accidentally */
1:  j 1b

    .weak exit
exit:
1:  j 1b

/* --------------------------------------------------------------------------
 * Vector table - weak default handlers for 127 peripheral IRQs
 * -------------------------------------------------------------------------- */

    .section .isr_vector, "ax"

    .weak nmi_handler
nmi_handler:
1:  j 1b

/* Default irq handler - infinite loop */
    .weak default_irq_handler
    .align 2
default_irq_handler:
1:  j 1b

/* Macro to create a weak default handler entry */
.macro IRQ_HANDLER irq
    .weak default_isr_\irq
    .set default_isr_\irq, default_irq_handler
.endm

/* Macro to create a vector table entry */
.macro VECTOR_ENTRY irq
    .long default_isr_\irq
.endm

/* Generate weak default handlers */
IRQ_HANDLER 1
IRQ_HANDLER 2
IRQ_HANDLER 3
IRQ_HANDLER 4
IRQ_HANDLER 5
IRQ_HANDLER 6
IRQ_HANDLER 7
IRQ_HANDLER 8
IRQ_HANDLER 9
IRQ_HANDLER 10
IRQ_HANDLER 11
IRQ_HANDLER 12
IRQ_HANDLER 13
IRQ_HANDLER 14
IRQ_HANDLER 15
IRQ_HANDLER 16
IRQ_HANDLER 17
IRQ_HANDLER 18
IRQ_HANDLER 19
IRQ_HANDLER 20
IRQ_HANDLER 21
IRQ_HANDLER 22
IRQ_HANDLER 23
IRQ_HANDLER 24
IRQ_HANDLER 25
IRQ_HANDLER 26
IRQ_HANDLER 27
IRQ_HANDLER 28
IRQ_HANDLER 29
IRQ_HANDLER 30
IRQ_HANDLER 31
IRQ_HANDLER 32
IRQ_HANDLER 33
IRQ_HANDLER 34
IRQ_HANDLER 35
IRQ_HANDLER 36
IRQ_HANDLER 37
IRQ_HANDLER 38
IRQ_HANDLER 39
IRQ_HANDLER 40
IRQ_HANDLER 41
IRQ_HANDLER 42
IRQ_HANDLER 43
IRQ_HANDLER 44
IRQ_HANDLER 45
IRQ_HANDLER 46
IRQ_HANDLER 47
IRQ_HANDLER 48
IRQ_HANDLER 49
IRQ_HANDLER 50
IRQ_HANDLER 51
IRQ_HANDLER 52
IRQ_HANDLER 53
IRQ_HANDLER 54
IRQ_HANDLER 55
IRQ_HANDLER 56
IRQ_HANDLER 57
IRQ_HANDLER 58
IRQ_HANDLER 59
IRQ_HANDLER 60
IRQ_HANDLER 61
IRQ_HANDLER 62
IRQ_HANDLER 63
IRQ_HANDLER 64
IRQ_HANDLER 65
IRQ_HANDLER 66
IRQ_HANDLER 67
IRQ_HANDLER 68
IRQ_HANDLER 69
IRQ_HANDLER 70
IRQ_HANDLER 71
IRQ_HANDLER 72
IRQ_HANDLER 73
IRQ_HANDLER 74
IRQ_HANDLER 75
IRQ_HANDLER 76
IRQ_HANDLER 77
IRQ_HANDLER 78
IRQ_HANDLER 79
IRQ_HANDLER 80
IRQ_HANDLER 81
IRQ_HANDLER 82
IRQ_HANDLER 83
IRQ_HANDLER 84
IRQ_HANDLER 85
IRQ_HANDLER 86
IRQ_HANDLER 87
IRQ_HANDLER 88
IRQ_HANDLER 89
IRQ_HANDLER 90
IRQ_HANDLER 91
IRQ_HANDLER 92
IRQ_HANDLER 93
IRQ_HANDLER 94
IRQ_HANDLER 95
IRQ_HANDLER 96
IRQ_HANDLER 97
IRQ_HANDLER 98
IRQ_HANDLER 99
IRQ_HANDLER 100
IRQ_HANDLER 101
IRQ_HANDLER 102
IRQ_HANDLER 103
IRQ_HANDLER 104
IRQ_HANDLER 105
IRQ_HANDLER 106
IRQ_HANDLER 107
IRQ_HANDLER 108
IRQ_HANDLER 109
IRQ_HANDLER 110
IRQ_HANDLER 111
IRQ_HANDLER 112
IRQ_HANDLER 113
IRQ_HANDLER 114
IRQ_HANDLER 115
IRQ_HANDLER 116
IRQ_HANDLER 117
IRQ_HANDLER 118
IRQ_HANDLER 119
IRQ_HANDLER 120
IRQ_HANDLER 121
IRQ_HANDLER 122
IRQ_HANDLER 123
IRQ_HANDLER 124
IRQ_HANDLER 125
IRQ_HANDLER 126
IRQ_HANDLER 127

/* Vector table section */
    .section .vector_table, "a"
    .global __vector_table
    .align 9

__vector_table:
    .long irq_handler_trap    /* Entry 0: synchronous exceptions / FreeRTOS trap */
    VECTOR_ENTRY 1            /* GPIO0_A */
    VECTOR_ENTRY 2            /* GPIO0_B */
    VECTOR_ENTRY 3            /* GPIO0_C */
    VECTOR_ENTRY 4            /* GPIO0_D */
    VECTOR_ENTRY 5            /* GPIO0_E */
    VECTOR_ENTRY 6            /* GPIO0_F */
    VECTOR_ENTRY 7            /* GPIO0_X */
    VECTOR_ENTRY 8            /* GPIO0_Y */
    VECTOR_ENTRY 9            /* GPIO0_Z */
    VECTOR_ENTRY 10           /* GPIO1_A */
    VECTOR_ENTRY 11           /* GPIO1_B */
    VECTOR_ENTRY 12           /* GPIO1_C */
    VECTOR_ENTRY 13           /* GPIO1_D */
    VECTOR_ENTRY 14           /* GPIO1_E */
    VECTOR_ENTRY 15           /* GPIO1_F */
    VECTOR_ENTRY 16           /* GPIO1_X */
    VECTOR_ENTRY 17           /* GPIO1_Y */
    VECTOR_ENTRY 18           /* GPIO1_Z */
    VECTOR_ENTRY 19           /* ADC0 */
    VECTOR_ENTRY 20           /* ADC1 */
    VECTOR_ENTRY 21           /* ADC2 */
    VECTOR_ENTRY 22           /* ADC3 */
    VECTOR_ENTRY 23           /* ACMP0 */
    VECTOR_ENTRY 24           /* ACMP1 */
    VECTOR_ENTRY 25           /* ACMP2 */
    VECTOR_ENTRY 26           /* ACMP3 */
    VECTOR_ENTRY 27           /* SPI0 */
    VECTOR_ENTRY 28           /* SPI1 */
    VECTOR_ENTRY 29           /* SPI2 */
    VECTOR_ENTRY 30           /* SPI3 */
    VECTOR_ENTRY 31           /* UART0 */
    VECTOR_ENTRY 32           /* UART1 */
    VECTOR_ENTRY 33           /* UART2 */
    VECTOR_ENTRY 34           /* UART3 */
    VECTOR_ENTRY 35           /* UART4 */
    VECTOR_ENTRY 36           /* UART5 */
    VECTOR_ENTRY 37           /* UART6 */
    VECTOR_ENTRY 38           /* UART7 */
    VECTOR_ENTRY 39           /* UART8 */
    VECTOR_ENTRY 40           /* UART9 */
    VECTOR_ENTRY 41           /* UART10 */
    VECTOR_ENTRY 42           /* UART11 */
    VECTOR_ENTRY 43           /* UART12 */
    VECTOR_ENTRY 44           /* UART13 */
    VECTOR_ENTRY 45           /* UART14 */
    VECTOR_ENTRY 46           /* UART15 */
    VECTOR_ENTRY 47           /* CAN0 */
    VECTOR_ENTRY 48           /* CAN1 */
    VECTOR_ENTRY 49           /* CAN2 */
    VECTOR_ENTRY 50           /* CAN3 */
    VECTOR_ENTRY 51           /* PTPC */
    VECTOR_ENTRY 52           /* WDG0 */
    VECTOR_ENTRY 53           /* WDG1 */
    VECTOR_ENTRY 54           /* WDG2 */
    VECTOR_ENTRY 55           /* WDG3 */
    VECTOR_ENTRY 56           /* MBX0A */
    VECTOR_ENTRY 57           /* MBX0B */
    VECTOR_ENTRY 58           /* MBX1A */
    VECTOR_ENTRY 59           /* MBX1B */
    VECTOR_ENTRY 60           /* GPTMR0 */
    VECTOR_ENTRY 61           /* GPTMR1 */
    VECTOR_ENTRY 62           /* GPTMR2 */
    VECTOR_ENTRY 63           /* GPTMR3 */
    VECTOR_ENTRY 64           /* GPTMR4 */
    VECTOR_ENTRY 65           /* GPTMR5 */
    VECTOR_ENTRY 66           /* GPTMR6 */
    VECTOR_ENTRY 67           /* GPTMR7 */
    VECTOR_ENTRY 68           /* I2C0 */
    VECTOR_ENTRY 69           /* I2C1 */
    VECTOR_ENTRY 70           /* I2C2 */
    VECTOR_ENTRY 71           /* I2C3 */
    VECTOR_ENTRY 72           /* PWM0 */
    VECTOR_ENTRY 73           /* HALL0 */
    VECTOR_ENTRY 74           /* QEI0 */
    VECTOR_ENTRY 75           /* PWM1 */
    VECTOR_ENTRY 76           /* HALL1 */
    VECTOR_ENTRY 77           /* QEI1 */
    VECTOR_ENTRY 78           /* PWM2 */
    VECTOR_ENTRY 79           /* HALL2 */
    VECTOR_ENTRY 80           /* QEI2 */
    VECTOR_ENTRY 81           /* PWM3 */
    VECTOR_ENTRY 82           /* HALL3 */
    VECTOR_ENTRY 83           /* QEI3 */
    VECTOR_ENTRY 84           /* SDP */
    VECTOR_ENTRY 85           /* XPI0 */
    VECTOR_ENTRY 86           /* XPI1 */
    VECTOR_ENTRY 87           /* XDMA */
    VECTOR_ENTRY 88           /* HDMA */
    VECTOR_ENTRY 89           /* FEMC */
    VECTOR_ENTRY 90           /* RNG */
    VECTOR_ENTRY 91           /* I2S0 */
    VECTOR_ENTRY 92           /* I2S1 */
    VECTOR_ENTRY 93           /* I2S2 */
    VECTOR_ENTRY 94           /* I2S3 */
    VECTOR_ENTRY 95           /* DAO */
    VECTOR_ENTRY 96           /* PDM */
    VECTOR_ENTRY 97           /* CAM0 */
    VECTOR_ENTRY 98           /* CAM1 */
    VECTOR_ENTRY 99           /* LCDC_D0 */
    VECTOR_ENTRY 100          /* LCDC_D1 */
    VECTOR_ENTRY 101          /* PDMA_D0 */
    VECTOR_ENTRY 102          /* PDMA_D1 */
    VECTOR_ENTRY 103          /* JPEG */
    VECTOR_ENTRY 104          /* NTMR0 */
    VECTOR_ENTRY 105          /* NTMR1 */
    VECTOR_ENTRY 106          /* USB0 */
    VECTOR_ENTRY 107          /* USB1 */
    VECTOR_ENTRY 108          /* ENET0 */
    VECTOR_ENTRY 109          /* ENET1 */
    VECTOR_ENTRY 110          /* SDXC0 */
    VECTOR_ENTRY 111          /* SDXC1 */
    VECTOR_ENTRY 112          /* PSEC */
    VECTOR_ENTRY 113          /* PGPIO */
    VECTOR_ENTRY 114          /* PWDG */
    VECTOR_ENTRY 115          /* PTMR */
    VECTOR_ENTRY 116          /* PUART */
    VECTOR_ENTRY 117          /* VAD */
    VECTOR_ENTRY 118          /* FUSE */
    VECTOR_ENTRY 119          /* SECMON */
    VECTOR_ENTRY 120          /* RTC */
    VECTOR_ENTRY 121          /* BUTN */
    VECTOR_ENTRY 122          /* BGPIO */
    VECTOR_ENTRY 123          /* BVIO */
    VECTOR_ENTRY 124          /* BROWNOUT */
    VECTOR_ENTRY 125          /* SYSCTL */
    VECTOR_ENTRY 126          /* DEBUG0 */
    VECTOR_ENTRY 127          /* DEBUG1 */
