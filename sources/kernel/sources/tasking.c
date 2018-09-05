/* Copyright © 2018 MAKER.                                                    */
/* This code is licensed under the MIT License.                               */
/* See: LICENSE.md                                                            */

#include <stdlib.h>
#include <string.h>

#include "libelf.h"
#include "sync/atomic.h"

#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "cpu/irq.h"

#include "kernel/filesystem.h"
#include "kernel/logger.h"
#include "kernel/memory.h"
#include "kernel/paging.h"
#include "kernel/system.h"
#include "kernel/tasking.h"

int PID = 0;
int TID = 0;

list_t *threads;
list_t *processes;

thread_t *alloc_thread(thread_entry_t entry, int flags)
{
    thread_t *thread = (thread_t *)malloc(sizeof(thread_t));
    memset(thread, 0, sizeof(thread_t));

    thread->id = TID++;

    thread->stack = malloc(STACK_SIZE);
    memset(thread->stack, 0, STACK_SIZE);

    thread->entry = entry;

    thread->esp = ((uint)(thread->stack) + STACK_SIZE);
    thread->esp -= sizeof(context_t);

    context_t *context = (context_t *)thread->esp;

    context->eflags = 0x202;
    context->eip = (u32)entry;
    context->ebp = ((uint)thread->stack + STACK_SIZE);

    if (flags & TASK_USER)
    {
        context->cs = 0x18;
        context->ds = 0x20;
        context->es = 0x20;
        context->fs = 0x20;
        context->gs = 0x20;
    }
    else
    {
        context->cs = 0x08;
        context->ds = 0x10;
        context->es = 0x10;
        context->fs = 0x10;
        context->gs = 0x10;
    }

    log("Thread with ID=%d allocated. (STACK=0x%x, ESP=0x%x)", thread->id, thread->stack, thread->esp);

    return thread;
}

process_t *alloc_process(const char *name, int flags)
{
    process_t *process = (process_t *)malloc(sizeof(process_t));

    process->id = PID++;

    strncpy(process->name, name, PROCNAME_SIZE);
    process->flags = flags;
    process->threads = list_alloc();

    if (flags & TASK_USER)
    {
        process->pdir = memory_alloc_pdir();
    }
    else
    {
        process->pdir = memory_kpdir();
    }

    log("Process '%s' with ID=%d allocated.", process->name, process->id);

    return process;
}

void free_thread(thread_t *thread)
{
    free(thread->stack);
    free(thread);
}

void free_process(process_t *process)
{
    if (process->pdir != memory_kpdir())
    {
        memory_free_pdir(process->pdir);
    }

    list_free(process->threads);
    free(process);
}

void kill_thread(thread_t *thread)
{
    list_remove(thread->process->threads, thread);

    if (thread->process->threads->count == 0)
    {
        free_process(thread->process);
    }

    free_thread(thread);
}

void kill_process(process_t *process)
{
    FOREACH(i, process->threads)
    {
        free_thread((thread_t *)i->value);
    }

    free_process(process);
}

thread_t *thread_get(THREAD thread)
{

    FOREACH(i, threads)
    {
        thread_t *t = (thread_t *)i->value;

        if (t->id == thread)
            return t;
    }

    return NULL;
}

process_t *process_get(PROCESS process)
{

    FOREACH(i, processes)
    {
        process_t *p = (process_t *)i->value;

        if (p->id == process)
            return p;
    }

    return NULL;
}

void notify_threads(bool is_thread, int handle, int outcode)
{
    FOREACH(i, threads)
    {
        thread_t *thread = i->value;

        bool is_waiting = (thread->state == THREAD_WAIT_THREAD && is_thread) ||
                          (thread->state == THREAD_WAIT_PROCESS && !is_thread);

        if (is_waiting && thread->waitinfo.handle == handle)
        {
            *thread->waitinfo.outcode = outcode;
            thread->state = THREAD_RUNNING;
        }
    }
}

/* --- Public functions ----------------------------------------------------- */

PROCESS kernel_process;
THREAD kernel_thread;

thread_t *running = NULL;
list_t *waiting;

esp_t shedule(esp_t esp, context_t *context);

void timer_set_frequency(int hz)
{
    u32 divisor = 1193180 / hz;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);

    log("Timer frequency is %dhz.", hz);
}

// define in cpu/boot.s
extern u32 __stack_bottom;

void tasking_setup()
{
    running = NULL;

    waiting = list_alloc();
    threads = list_alloc();
    processes = list_alloc();

    kernel_process = process_create("kernel", 0);
    kernel_thread = thread_create(kernel_process, NULL, NULL, 0);

    // Set the correct stack for the kernel main stack
    thread_t *kthread = thread_get(kernel_thread);
    free(kthread->stack);
    kthread->stack = &__stack_bottom;
    kthread->esp = ((uint)(kthread->stack) + STACK_SIZE);

    timer_set_frequency(100);
    irq_register(0, (irq_handler_t)&shedule);
}

/* --- Thread managment ----------------------------------------------------- */

THREAD thread_self()
{
    if (running == NULL)
        return -1;
    return running->id;
}

THREAD thread_create(PROCESS p, thread_entry_t entry, void *arg, int flags)
{
    UNUSED(arg);

    atomic_begin();

    process_t *process = process_get(p);
    thread_t *thread = alloc_thread(entry, process->flags | flags);

    list_pushback(process->threads, thread);
    list_pushback(threads, thread);
    thread->process = process;

    if (running != NULL)
    {
        list_pushback(waiting, thread);
    }
    else
    {
        running = thread;
    }

    thread->state = THREAD_RUNNING;

    atomic_end();

    log("Thread with ID=%d child of process '%s' (ID=%d) is running.", thread->id, process->name, process->id);

    return thread->id;
}

int thread_cancel(THREAD t)
{
    atomic_begin();

    thread_t *thread;

    if ((thread = thread_get(t)))
    {
        thread->state = THREAD_CANCELED;
        notify_threads(1, thread_self(), 0);
        log("Thread n°%d got canceled.", t);
    }

    atomic_end();

    return thread == NULL; // return 1 if canceling the thread failled!
}

void thread_exit(void *retval)
{
    atomic_begin();

    thread_t *thread = thread_get(thread_self());
    thread->state = THREAD_CANCELED;
    notify_threads(1, thread_self(), (int)retval);

    log("Thread n°%d exited with value 0x%x.", thread->id, retval);

    atomic_end();

    while (1)
        hlt();
}

void thread_dump_all()
{
    atomic_begin();

    printf("\n\tThreads:");

    FOREACH(i, threads)
    {
        thread_dump(((thread_t *)i->value)->id);
    }

    atomic_end();
}

void thread_dump(THREAD t)
{
    atomic_begin();

    thread_t *thread = thread_get(t);

    printf("\n\tThread ID=%d child of process '%s' ID=%d.", t, thread->process->name, thread->process->id);
    printf("(ESP=0x%x STACK=%x STATE=%x)", thread->esp, thread->stack, thread->state);

    atomic_end();
}

/* --- Process managment ---------------------------------------------------- */

PROCESS process_self()
{
    if (running == NULL)
        return -1;
    return running->process->id;
}

PROCESS process_create(const char *name, int flags)
{
    atomic_begin();

    process_t *process = alloc_process(name, flags);
    list_pushback(processes, process);

    atomic_end();

    log("Process '%s' with ID=%d is running.", process->name, process->id);

    return process->id;
}

void load_elfseg(process_t *process, uint src, uint srcsz, uint dest, uint destsz)
{
    log("Loading ELF segment: SRC=0x%x(%d) DEST=0x%x(%d)", src, srcsz, dest, destsz);

    if (dest >= 0x100000)
    {
        atomic_begin();

        // To avoid pagefault we need to switch page directorie.
        page_directorie_t *pdir = running->process->pdir;

        paging_load_directorie(process->pdir);

        process_map(process->id, dest, PAGE_ALIGN(destsz) / PAGE_SIZE);
        memset((void *)dest, 0, destsz);
        memcpy((void *)dest, (void *)src, srcsz);

        paging_load_directorie(pdir);

        atomic_end();
    }
    else
    {
        log("Elf segment ignored, not in user memory!");
    }
}

PROCESS process_exec(const char *path, int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);

    file_t *fp = file_open(NULL, path);

    if (!fp)
    {
        log("EXEC: %s file not found, exec failed!", path);
        return 0;
    }

    PROCESS p = process_create(path, 0);

    void *buffer = file_read_all(fp);
    file_close(fp);

    ELF_header_t *elf = (ELF_header_t *)buffer;

    log("ELF file: VALID=%d TYPE=%d ENTRY=0x%x SEG_COUNT=%i", ELF_valid(elf), elf->type, elf->entry, elf->phnum);

    ELF_program_t program;

    for (int i = 0; ELF_read_program(elf, &program, i); i++)
    {
        printf("\n");
        load_elfseg(process_get(p), (uint)(buffer) + program.offset, program.filesz, program.vaddr, program.memsz);
    }

    thread_create(p, (thread_entry_t)elf->entry, NULL, 0);

    free(buffer);

    return p;
}

void process_cancel(PROCESS p)
{
    atomic_begin();

    if (p != kernel_process)
    {

        process_t *process = process_get(p);
        process->state = PROCESS_CANCELED;
        notify_threads(0, p, -1);

        log("Process '%s' ID=%d canceled!", process->name, process->id);
    }
    else
    {
        process_t *process = process_get(process_self());
        log("Warning! Process '%s' ID=%d tried to commit murder on the kernel!", process->name, process_self());
    }

    atomic_end();
}

void process_exit(int code)
{
    atomic_begin();

    PROCESS p = process_self();
    process_t *process = process_get(p);

    if (p != kernel_process)
    {
        process->state = PROCESS_CANCELED;
        log("Process '%s' ID=%d exited with code %d.", process->name, process->id, code);

        // Notify waiting thread that we exited.
        notify_threads(0, p, code);
    }
    else
    {
        log("Warning! Kernel try to commit suicide!");
    }

    atomic_end();

    while (1)
        hlt();
}

int process_map(PROCESS p, uint addr, uint count)
{
    return memory_map(process_get(p)->pdir, addr, count, 1);
}

int process_unmap(PROCESS p, uint addr, uint count)
{
    return memory_unmap(process_get(p)->pdir, addr, count);
}

/* --- Sheduler ------------------------------------------------------------- */

uint ticks = 0;

void sanity_check(thread_t *thread)
{
    uint stack = (uint)thread->stack;

    if (!(thread->esp >= stack && thread->esp <= stack + STACK_SIZE))
    {
        PANIC("Thread ID=%d failed sanity check! (ESP=0x%x STACK=0x%x)", thread->id, thread->esp, thread->stack);
    }
}

esp_t shedule(esp_t esp, context_t *context)
{
    ticks++;

    UNUSED(context);

    // int delta = (running->esp - esp);
    // printf("esp=%x -> %x (%d) EIP=%x ID=%d\n", running->esp, esp, delta, context->eip, running->id);

    // Save the old context
    running->esp = esp;
    sanity_check(running);

    list_pushback(waiting, running);
    list_pop(waiting, (void *)&running);


    // Load the new context
    set_kernel_stack((uint)running->stack + STACK_SIZE);
    paging_load_directorie(running->process->pdir);

    return running->esp;
}
