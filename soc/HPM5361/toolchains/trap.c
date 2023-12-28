/*
 * Copyright (c) 2021 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "hpm_common.h"
#include "hpm_soc.h"

/********************** MCAUSE exception types **************************************/
#define MCAUSE_INSTR_ADDR_MISALIGNED (0U)       /* !< Instruction Address misaligned */
#define MCAUSE_INSTR_ACCESS_FAULT (1U)          /* !< Instruction access fault */
#define MCAUSE_ILLEGAL_INSTR (2U)               /* !< Illegal instruction */
#define MCAUSE_BREAKPOINT (3U)                  /* !< Breakpoint */
#define MCAUSE_LOAD_ADDR_MISALIGNED (4U)        /* !< Load address misaligned */
#define MCAUSE_LOAD_ACCESS_FAULT (5U)           /* !< Load access fault */
#define MCAUSE_STORE_AMO_ADDR_MISALIGNED (6U)   /* !< Store/AMO address misaligned */
#define MCAUSE_STORE_AMO_ACCESS_FAULT (7U)      /* !< Store/AMO access fault */
#define MCAUSE_ECALL_FROM_USER_MODE (8U)        /* !< Environment call from User mode */
#define MCAUSE_ECALL_FROM_SUPERVISOR_MODE (9U)  /* !< Environment call from Supervisor mode */
#define MCAUSE_ECALL_FROM_MACHINE_MODE (11U)    /* !< Environment call from machine mode */
#define MCAUSE_INSTR_PAGE_FAULT (12U)           /* !< Instruction page fault */
#define MCAUSE_LOAD_PAGE_FAULT (13)             /* !< Load page fault */
#define MCAUSE_STORE_AMO_PAGE_FAULT (15U)       /* !< Store/AMO page fault */

#define IRQ_S_SOFT            1
#define IRQ_H_SOFT            2
#define IRQ_M_SOFT            3
#define IRQ_S_TIMER            5
#define IRQ_H_TIMER            6
#define IRQ_M_TIMER            7
#define IRQ_S_EXT            9
#define IRQ_H_EXT            10
#define IRQ_M_EXT            11
#define IRQ_COP                12
#define IRQ_HOST                13

__attribute__((weak)) void mchtmr_isr(void)
{
}

__attribute__((weak)) void swi_isr(void)
{
}

__attribute__((weak)) void syscall_handler(long n, long a0, long a1, long a2, long a3)
{
    (void) n;
    (void) a0;
    (void) a1;
    (void) a2;
    (void) a3;
}

__attribute__((weak)) long exception_handler(long cause, long epc)
{
    switch (cause) {
    case MCAUSE_INSTR_ADDR_MISALIGNED:
        break;
    case MCAUSE_INSTR_ACCESS_FAULT:
        break;
    case MCAUSE_ILLEGAL_INSTR:
        break;
    case MCAUSE_BREAKPOINT:
        break;
    case MCAUSE_LOAD_ADDR_MISALIGNED:
        break;
    case MCAUSE_LOAD_ACCESS_FAULT:
        break;
    case MCAUSE_STORE_AMO_ADDR_MISALIGNED:
        break;
    case MCAUSE_STORE_AMO_ACCESS_FAULT:
        break;
    case MCAUSE_ECALL_FROM_USER_MODE:
        break;
    case MCAUSE_ECALL_FROM_SUPERVISOR_MODE:
        break;
    case MCAUSE_ECALL_FROM_MACHINE_MODE:
        break;
    case MCAUSE_INSTR_PAGE_FAULT:
        break;
    case MCAUSE_LOAD_PAGE_FAULT:
        break;
    case MCAUSE_STORE_AMO_PAGE_FAULT:
        break;
    default:
        break;
    }
    /* Unhandled Trap */
    return epc;
}

#if !defined(CONFIG_FREERTOS) && !defined(CONFIG_UCOS_III) && !defined(CONFIG_THREADX) && !defined(CONFIG_RTTHREAD)
void irq_handler_trap(void) __attribute__ ((section(".isr_vector"), interrupt("machine"), aligned(4)));
#else
void irq_handler_trap(void) __attribute__ ((section(".isr_vector")));
#endif
void irq_handler_trap(void)
{
    long mcause = read_csr(CSR_MCAUSE);
    long mepc = read_csr(CSR_MEPC);
    long mstatus = read_csr(CSR_MSTATUS);
#if defined(SUPPORT_PFT_ARCH) && SUPPORT_PFT_ARCH
    long mxstatus = read_csr(CSR_MXSTATUS);
#endif
#ifdef __riscv_dsp
    int ucode = read_csr(CSR_UCODE);
#endif
#ifdef __riscv_flen
    int fcsr = read_fcsr();
#endif

    /* clobbers list for ecall */
#ifdef __riscv_32e
    __asm volatile("" : : : "t0", "a0", "a1", "a2", "a3");
#else
    __asm volatile("" : : : "a7", "a0", "a1", "a2", "a3");
#endif

    /* Do your trap handling */
    if ((mcause & CSR_MCAUSE_INTERRUPT_MASK) && ((mcause & CSR_MCAUSE_EXCEPTION_CODE_MASK) == IRQ_M_TIMER)) {
        /* Machine timer interrupt */
        mchtmr_isr();
    }
#ifdef USE_NONVECTOR_MODE
    else if ((mcause & CSR_MCAUSE_INTERRUPT_MASK) && ((mcause & CSR_MCAUSE_EXCEPTION_CODE_MASK) == IRQ_M_EXT)) {

        typedef void(*isr_func_t)(void);

        /* Machine-level interrupt from PLIC */
        uint32_t irq_index = __plic_claim_irq(HPM_PLIC_BASE, HPM_PLIC_TARGET_M_MODE);
        if (irq_index) {
        /* Workaround: irq number returned by __plic_claim_irq might be 0, which is caused by plic. So skip invalid irq_index as a workaround */
#if !defined(DISABLE_IRQ_PREEMPTIVE) || (DISABLE_IRQ_PREEMPTIVE == 0)
            enable_global_irq(CSR_MSTATUS_MIE_MASK);
#endif
            ((isr_func_t)__vector_table[irq_index])();
            __plic_complete_irq(HPM_PLIC_BASE, HPM_PLIC_TARGET_M_MODE, irq_index);
        }
    }
#endif

    else if ((mcause & CSR_MCAUSE_INTERRUPT_MASK) && ((mcause & CSR_MCAUSE_EXCEPTION_CODE_MASK) == IRQ_M_SOFT)) {
        /* Machine SWI interrupt */
        intc_m_claim_swi();
        swi_isr();
        intc_m_complete_swi();
    } else if (!(mcause & CSR_MCAUSE_INTERRUPT_MASK) && ((mcause & CSR_MCAUSE_EXCEPTION_CODE_MASK) == MCAUSE_ECALL_FROM_MACHINE_MODE)) {
        /* Machine Syscal call */
        __asm volatile(
        "mv a4, a3\n"
        "mv a3, a2\n"
        "mv a2, a1\n"
        "mv a1, a0\n"
        #ifdef __riscv_32e
        "mv a0, t0\n"
        #else
        "mv a0, a7\n"
        #endif
        "jalr %0\n"
        : : "r"(syscall_handler) : "a4"
        );
        mepc += 4;
    } else {
        mepc = exception_handler(mcause, mepc);
    }

    /* Restore CSR */
    write_csr(CSR_MSTATUS, mstatus);
    write_csr(CSR_MEPC, mepc);
#if defined(SUPPORT_PFT_ARCH) && SUPPORT_PFT_ARCH
    write_csr(CSR_MXSTATUS, mxstatus);
#endif
#ifdef __riscv_dsp
    write_csr(CSR_UCODE, ucode);
#endif
#ifdef __riscv_flen
    write_fcsr(fcsr);
#endif
}
