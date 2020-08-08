/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#include <autoconf.h>
#include <utils/util.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <cspace/cspace.h>
#include <aos/sel4_zf_logif.h>
#include <aos/debug.h>

// #include <clock/clock.h>
#include <clock/device.h>
#include <cpio/cpio.h>
#include <serial/serial.h>

#include <sel4runtime.h>
#include <sel4runtime/auxv.h>

#include "bootstrap.h"
#include "irq.h"
#include "network.h"
#include "vm/frame_table.h"
#include "drivers/uart.h"
#include "ut.h"
#include "vmem_layout.h"
#include "mapping.h"
#include "syscalls.h"
#include "tests.h"
#include "utils.h"
#include "threads.h"
#include "process.h"
#include "syscalls/syscall.h"
#include "fs/console.h"
#include "vm/fault_handler.h"

#include <aos/vsyscall.h>

/*
 * To differentiate between signals from notification objects and and IPC messages,
 * we assign a badge to the notification object. The badge that we receive will
 * be the bitwise 'OR' of the notification object badge and the badges
 * of all pending IPC messages.
 *
 * All badged IRQs set high bet, then we use uniqe bits to
 * distinguish interrupt sources.
 */
#define IRQ_EP_BADGE         BIT(seL4_BadgeBits - 1ul)
#define IRQ_IDENT_BADGE_BITS MASK(seL4_BadgeBits - 1ul)


#define MAX_CONCURRENT_IRQS 10


/* provided by gcc */
extern void (__register_frame)(void *);

/* root tasks cspace */
cspace_t cspace;

process_t *currproc;

seL4_CPtr timer_cptr;
void *timer_vaddr;

static seL4_CPtr ipc_ep;
seL4_CPtr timer_ep;
seL4_IRQHandler *timer_irq_handler;

typedef void (*timer_callback_t)(uint32_t id, void *data);
extern pid_t clock_driver_pid;

int timer_irq(
    void *data,
    seL4_Word irq,
    seL4_IRQHandler irq_handler
) {
    seL4_Word irq_badge_queue[MAX_CONCURRENT_IRQS];
    int irq_queue_len = -1;
    printf("timer irq\n");
    seL4_Send(timer_ep, seL4_MessageInfo_new(0, 0, 0, 1));
    seL4_Word badge = 0;
    // stupid mcs, how do i call recv without a reply obj
    // i don't want to reply cuz i'm mean
    seL4_CPtr reply;
    ut_t *reply_ut = alloc_retype(&reply, seL4_ReplyObject, seL4_ReplyBits);
    while (1) {
        //printf("w\n");
        seL4_MessageInfo_t msg = seL4_Recv(timer_ep, &badge, reply);
        //printf("wr\n");
        printf("%d %d\n", seL4_MessageInfo_get_label(msg), seL4_MessageInfo_get_length(msg));
        printf("%ld %ld %ld %ld\n", badge, seL4_GetMR(0), seL4_GetMR(1), seL4_GetMR(2));
        if (badge & IRQ_EP_BADGE) {
            /* It's a notification from our bound notification
             * object! We can't handle this here since irq handler could potentially block the kernel
             * by calling into clock driver again. instead, we add it to a queue and deal with them
             * after we finish handling timer irq */
            if (++irq_queue_len == MAX_CONCURRENT_IRQS) {
                ZF_LOGF("too many IRQs in timer_irq routine. Please increase MAX_CONCURRENT_IRQS");
            }
            printf("queueing irq %ld\n", badge);
            irq_badge_queue[irq_queue_len] = badge;
            continue;
        }
        if (badge != PID_TO_BADGE(clock_driver_pid)) {
            printf("badge %ld != clock %ld", badge, PID_TO_BADGE(clock_driver_pid));
            continue;
        }
        printf("%ld %ld\n", seL4_MessageInfo_get_label(msg), seL4_MessageInfo_get_length(msg));
        printf("%ld %ld %ld %ld\n", badge, seL4_GetMR(0), seL4_GetMR(1), seL4_GetMR(2));
        if (seL4_MessageInfo_get_length(msg) == 3) {
            unsigned int id = seL4_GetMR(0);
            timer_callback_t cb = seL4_GetMR(1);
            void *data = seL4_GetMR(2);
            printf("cb\n");
            cb(id, data);
            printf("cbb\n");
        } else {
            break;
        }
    }
    cspace_delete(&cspace, reply);
    cspace_free_slot(&cspace, reply);
    ut_free(reply_ut);
    seL4_Error ack_err = seL4_IRQHandler_Ack(irq_handler);
    printf("%d\n", ack_err);
    for (int i = 0; i <= irq_queue_len; i++) {
        sos_handle_irq_notification(irq_badge_queue + i);
    }
    if (ack_err != seL4_NoError) return ack_err;

    return seL4_NoError;
}

NORETURN void syscall_loop(seL4_CPtr ep)
{
    while (1) {
        seL4_CPtr reply;
        /* Create reply object */
        ut_t *reply_ut = alloc_retype(&reply, seL4_ReplyObject, seL4_ReplyBits);
        if (reply_ut == NULL) {
            ZF_LOGF("Failed to alloc reply object ut");
        }

        seL4_Word badge = 0;
        /* Block on ep, waiting for an IPC sent over ep, or
         * a notification from our bound notification object */
        seL4_MessageInfo_t message = seL4_Recv(ep, &badge, reply);
        /* Awake! We got a message - check the label and badge to
         * see what the message is about */
        seL4_Word label = seL4_MessageInfo_get_label(message);

        process_t *proc = get_process_by_pid(BADGE_TO_PID(badge));
        currproc = proc;

        // printf("got msg from badge %lld\n", badge);

        if (badge & IRQ_EP_BADGE) {
            /* It's a notification from our bound notification
             * object! */
            // printf("handle irq %lld\n", IRQ_EP_BADGE);
            sos_handle_irq_notification(&badge);
            cspace_delete(&cspace, reply);
            cspace_free_slot(&cspace, reply);
            ut_free(reply_ut);
        } else if (label == seL4_Fault_VMFault) {
            debug_print_fault(message, proc->command);
            seL4_Fault_t fault = seL4_getFault(message);
            handle_vm_fault(&cspace, seL4_Fault_VMFault_get_Addr(fault), seL4_Fault_VMFault_get_FSR(fault), proc, reply, reply_ut);
            printf("finished handle vm fault\n");
        } else if (label == seL4_Fault_NullFault) {
            /* It's not a fault or an interrupt, it must be an IPC
             * message from tty_test! */
            handle_syscall(&cspace, badge, seL4_MessageInfo_get_length(message) - 1, reply, reply_ut, proc);
        } else {
            ZF_LOGE("fault!\n");
            handle_fault_kill(proc);
        }
    }
}



/* Allocate an endpoint and a notification object for sos.
 * Note that these objects will never be freed, so we do not
 * track the allocated ut objects anywhere
 */
static void sos_ipc_init(seL4_CPtr *ipc_ep, seL4_CPtr *timer_ep, seL4_CPtr *ntfn)
{
    /* Create an notification object for interrupts */
    ut_t *ut = alloc_retype(ntfn, seL4_NotificationObject, seL4_NotificationBits);
    ZF_LOGF_IF(!ut, "No memory for notification object");

    /* Bind the notification object to our TCB */
    seL4_Error err = seL4_TCB_BindNotification(seL4_CapInitThreadTCB, *ntfn);
    ZF_LOGF_IFERR(err, "Failed to bind notification object to TCB");

    /* Create an endpoint for user application IPC */
    ut = alloc_retype(ipc_ep, seL4_EndpointObject, seL4_EndpointBits);
    ZF_LOGF_IF(!ut, "No memory for endpoint");

    /* Create an endpoint for timer driver IPC */
    ut = alloc_retype(timer_ep, seL4_EndpointObject, seL4_EndpointBits);
    ZF_LOGF_IF(!ut, "No memory for endpoint");
}

/* called by crt */
seL4_CPtr get_seL4_CapInitThreadTCB(void)
{
    return seL4_CapInitThreadTCB;
}

/* tell muslc about our "syscalls", which will bve called by muslc on invocations to the c library */
void init_muslc(void)
{
    muslcsys_install_syscall(__NR_set_tid_address, sys_set_tid_address);
    muslcsys_install_syscall(__NR_writev, sys_writev);
    muslcsys_install_syscall(__NR_exit, sys_exit);
    muslcsys_install_syscall(__NR_rt_sigprocmask, sys_rt_sigprocmask);
    muslcsys_install_syscall(__NR_gettid, sys_gettid);
    muslcsys_install_syscall(__NR_getpid, sys_getpid);
    muslcsys_install_syscall(__NR_tgkill, sys_tgkill);
    muslcsys_install_syscall(__NR_tkill, sys_tkill);
    muslcsys_install_syscall(__NR_exit_group, sys_exit_group);
    muslcsys_install_syscall(__NR_ioctl, sys_ioctl);
    muslcsys_install_syscall(__NR_mmap, sys_mmap);
    muslcsys_install_syscall(__NR_brk,  sys_brk);
    muslcsys_install_syscall(__NR_clock_gettime, sys_clock_gettime);
    muslcsys_install_syscall(__NR_nanosleep, sys_nanosleep);
    muslcsys_install_syscall(__NR_getuid, sys_getuid);
    muslcsys_install_syscall(__NR_getgid, sys_getgid);
    muslcsys_install_syscall(__NR_openat, sys_openat);
    muslcsys_install_syscall(__NR_close, sys_close);
    muslcsys_install_syscall(__NR_socket, sys_socket);
    muslcsys_install_syscall(__NR_bind, sys_bind);
    muslcsys_install_syscall(__NR_listen, sys_listen);
    muslcsys_install_syscall(__NR_connect, sys_connect);
    muslcsys_install_syscall(__NR_accept, sys_accept);
    muslcsys_install_syscall(__NR_sendto, sys_sendto);
    muslcsys_install_syscall(__NR_recvfrom, sys_recvfrom);
    muslcsys_install_syscall(__NR_readv, sys_readv);
    muslcsys_install_syscall(__NR_getsockname, sys_getsockname);
    muslcsys_install_syscall(__NR_getpeername, sys_getpeername);
    muslcsys_install_syscall(__NR_fcntl, sys_fcntl);
    muslcsys_install_syscall(__NR_setsockopt, sys_setsockopt);
    muslcsys_install_syscall(__NR_getsockopt, sys_getsockopt);
    muslcsys_install_syscall(__NR_ppoll, sys_ppoll);
    muslcsys_install_syscall(__NR_madvise, sys_madvise);
}

static void start_sosh() {
    /* Start the user application */
    printf("Start first process\n");
    bool success = start_first_process(&cspace, TTY_NAME, ipc_ep, timer_ep);
    ZF_LOGF_IF(!success, "Failed to start first process");
}

NORETURN void *main_continued(UNUSED void *arg)
{
    /* Initialise other system compenents here */
    seL4_CPtr ntfn;
    sos_ipc_init(&ipc_ep, &timer_ep, &ntfn);
    sos_init_irq_dispatch(
        &cspace,
        seL4_CapIRQControl,
        ntfn,
        IRQ_EP_BADGE,
        IRQ_IDENT_BADGE_BITS
    );
    frame_table_init(&cspace, seL4_CapInitThreadVSpace);

    /* run sos initialisation tests */
    run_tests(&cspace);

    /* Map the timer device (NOTE: this is the same mapping you will use for your timer driver -
     * sos uses the watchdog timers on this page to implement reset infrastructure & network ticks,
     * so touching the watchdog timers here is not recommended!) */
    timer_vaddr = sos_map_device(&cspace, PAGE_ALIGN_4K(TIMER_MAP_BASE), PAGE_SIZE_4K, &timer_cptr);

    process_init();

    /* Initialise the network hardware. */
    printf("Network init\n");
    network_init(&cspace, timer_vaddr, ntfn, start_sosh);

    /* Initialises the timer */
    // printf("Timer init\n");
    // start_timer(timer_vaddr);
    /* You will need to register an IRQ handler for the timer here.
     * See "irq.h". */
    sos_register_irq_handler(42, true, timer_irq, NULL, timer_irq_handler);

    /* Initialize syscall table */
    init_syscall();

    /* Initialize console */
    console_init();

    printf("\nSOS entering syscall loop\n");
    init_threads(ipc_ep, sched_ctrl_start, sched_ctrl_end);
    syscall_loop(ipc_ep);
}

/*
 * Main entry point - called by crt.
 */
int main(void)
{
    init_muslc();

    /* register the location of the unwind_tables -- this is required for
     * backtrace() to work */
    __register_frame(&__eh_frame_start);

    seL4_BootInfo *boot_info = sel4runtime_bootinfo();

    debug_print_bootinfo(boot_info);

    printf("\nSOS Starting...\n");

    NAME_THREAD(seL4_CapInitThreadTCB, "SOS:root");

    sched_ctrl_start = boot_info->schedcontrol.start;
    sched_ctrl_end = boot_info->schedcontrol.end;

    /* Initialise the cspace manager, ut manager and dma */
    sos_bootstrap(&cspace, boot_info);

    /* switch to the real uart to output (rather than seL4_DebugPutChar, which only works if the
     * kernel is built with support for printing, and is much slower, as each character print
     * goes via the kernel)
     *
     * NOTE we share this uart with the kernel when the kernel is in debug mode. */
    uart_init(&cspace);
    update_vputchar(uart_putchar);

    /* test print */
    printf("SOS Started!\n");

    /* allocate a bigger stack and switch to it -- we'll also have a guard page, which makes it much
     * easier to detect stack overruns */
    seL4_Word vaddr = SOS_STACK;
    for (int i = 0; i < SOS_STACK_PAGES; i++) {
        seL4_CPtr frame_cap;
        ut_t *frame = alloc_retype(&frame_cap, seL4_ARM_SmallPageObject, seL4_PageBits);
        ZF_LOGF_IF(frame == NULL, "Failed to allocate stack page");
        seL4_Error err = map_frame(&cspace, frame_cap, seL4_CapInitThreadVSpace,
                                   vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
        ZF_LOGF_IFERR(err, "Failed to map stack");
        vaddr += PAGE_SIZE_4K;
    }

    utils_run_on_stack((void *) vaddr, main_continued, NULL);

    UNREACHABLE();
}


