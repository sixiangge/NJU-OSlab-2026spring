#include "proc.h"
#include "device.h"
#include "syscall.h"
#include "fat.h"

struct PCB pcb[MAX_PCB];
struct PCB *current = NULL;
struct Semaphore semaphores[MAX_SEM];

// --- Process Scheduling (Round-Robin) ---
void schedule() {
    struct PCB *next = NULL;
    int start = (current) ? (current->pid + 1) % MAX_PCB : 0;

    for (int i = 0; i < MAX_PCB; i++) {
        int idx = (start + i) % MAX_PCB;
        if (idx == 0) continue; 
        if (pcb[idx].state == RUNNABLE || pcb[idx].state == RUNNING) {
            next = &pcb[idx];
            break;
        }
    }

    if (!next && (pcb[0].state == RUNNABLE || pcb[0].state == RUNNING)) {
        next = &pcb[0];
    }

    if (next && next != current) {
        if (current && current->state == RUNNING) {
            current->state = RUNNABLE;
        }
        next->state = RUNNING;
        next->time_count = 5; 
        current = next;
        
        extern TSS tss;
        tss.esp0 = (uint32_t)next->kstack + KSTACK_SIZE;
        update_user_seg_base(next->mem_base, next->mem_limit);
    } else if (current) {
        if (current->time_count <= 0) current->time_count = 5;
    }
}

int do_fork() {
    int child_idx = -1;
    for (int i = 1; i < MAX_PCB; i++) {
        if (pcb[i].state == UNUSED) { child_idx = i; break; }
    }
    if (child_idx == -1) return -1;

    struct PCB *child = &pcb[child_idx];
    struct PCB *parent = current;

    uint8_t *src = (uint8_t *)parent->mem_base;
    uint8_t *dst = (uint8_t *)child->mem_base;
    for (uint32_t i = 0; i < SLOT_SIZE; i++) dst[i] = src[i];

    uint32_t offset = (uint32_t)parent->tf - (uint32_t)parent->kstack;
    child->tf = (struct TrapFrame *)((uint32_t)child->kstack + offset);
    for (int i = 0; i < KSTACK_SIZE; i++) child->kstack[i] = parent->kstack[i];

    child->tf->eax = 0;
    child->pid = child_idx;
    child->ppid = parent->pid;
    child->state = RUNNABLE;
    child->sleep_ticks = 0;
    child->time_count = 5;
    child->mem_limit = parent->mem_limit;
    child->vfork_count = 0;
    // TODO: Copy each parent fd pointer into child->fds[] and increment ref_count
    // for every non-null shared FileDescription.
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        child->fds[i] = parent->fds[i];
        if (child->fds[i] != NULL) child->fds[i]->ref_count++;
    }
    child->next = NULL;

    schedule();
    return child->pid;
}

int do_vfork() {
    int child_idx = -1;
    for (int i = 1; i < MAX_PCB; i++) {
        if (pcb[i].state == UNUSED) { child_idx = i; break; }
    }
    if (child_idx == -1) return -1;

    struct PCB *parent = current;
    parent->vfork_count++;
    if (parent->vfork_count > 1) {
        parent->vfork_count--;
        return -1;
    }

    struct PCB *child = &pcb[child_idx];
    child->mem_base = parent->mem_base;
    child->mem_limit = parent->mem_limit;

    uint32_t offset = (uint32_t)parent->tf - (uint32_t)parent->kstack;
    child->tf = (struct TrapFrame *)((uint32_t)child->kstack + offset);
    for (int i = 0; i < KSTACK_SIZE; i++) child->kstack[i] = parent->kstack[i];

    child->tf->eax = 0;
    child->tf->esp_user = parent->tf->esp_user - VFORK_STACK_OFFSET;
    child->tf->ebp = parent->tf->ebp - VFORK_STACK_OFFSET;

    // Stack Copy: Ensure child inherits the environment to return from vfork.
    // Critical Fix: Adjust child's EBP (frame pointer) values on the stack because
    // the child's stack is at a different virtual address than the parent's
    // (VFORK_STACK_OFFSET lower). Without this, popping EBP would restore a 
    // parent-space address, causing stack corruption upon function return.
    uint32_t stack_len = SEG_LIMIT - parent->tf->esp_user;
    if (stack_len > 0 && stack_len < VFORK_STACK_OFFSET) {
        uint32_t *src_stack = (uint32_t *)(parent->mem_base + parent->tf->esp_user);
        uint32_t *dst_stack = (uint32_t *)(child->mem_base + child->tf->esp_user);
        
        // EBP adjustment: Fix stack frame pointers to point to child's stack
        for (uint32_t i = 0; i < stack_len / 4; i++) {
            uint32_t val = src_stack[i];
            if (val >= parent->tf->esp_user && val < SEG_LIMIT) {
                dst_stack[i] = val - VFORK_STACK_OFFSET;
            } else {
                dst_stack[i] = val;
            }
        }
        
        // Copy remaining bytes if not 4-byte aligned (though stack usually is)
        uint32_t remainder = stack_len % 4;
        if (remainder > 0) {
            uint8_t *src_byte = (uint8_t *)src_stack;
            uint8_t *dst_byte = (uint8_t *)dst_stack;
            for (uint32_t i = stack_len - remainder; i < stack_len; i++) {
                dst_byte[i] = src_byte[i];
            }
        }
    }

    child->pid = child_idx;
    child->ppid = parent->pid;
    child->state = RUNNABLE;
    child->sleep_ticks = 0;
    child->time_count = 5;
    child->vfork_count = 0;
    // TODO: Copy each parent fd pointer into the vfork child's fds[] and increment
    // ref_count for every non-null shared FileDescription.
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        child->fds[i] = parent->fds[i];
        if (child->fds[i] != NULL) child->fds[i]->ref_count++;
    }
    child->next = NULL;

    schedule();
    return child->pid;
}

void do_exec(const char *path) {
    if (current->pid > 0) {
        uint32_t correct_base = 0x20000 + (current->pid - 1) * SLOT_SIZE;
        if (current->mem_base != correct_base) {
            current->mem_base = correct_base;
            if (current->ppid >= 0 && current->ppid < MAX_PCB) pcb[current->ppid].vfork_count--;
        }
    }
    
    // TODO: Before replacing the process image, close all old fds with fat_close(), open path
    // with fat_open(), read the executable into current->mem_base, then close it.
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (current->fds[i] != NULL) fat_close(i);
    }

    int fd = fat_open(path);
    if (fd >= 0 && current->fds[fd] != NULL) {
        uint32_t size = current->fds[fd]->file_size;
        fat_read(fd, (void *)current->mem_base, size);
        fat_close(fd);
    }

    current->mem_limit = SEG_LIMIT;
    update_user_seg_base(current->mem_base, current->mem_limit);
    current->tf->eip = 0;
    current->tf->esp_user = SEG_LIMIT; 
    current->tf->eax = 0;
}

void do_exit() {
    current->state = ZOMBIE;
    for (int i = 0; i < MAX_PCB; i++) {
        if (pcb[i].ppid == current->pid && pcb[i].state != UNUSED) pcb[i].ppid = 0;
    }

    // TODO: Close all open files on exit with fat_close()
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (current->fds[i] != NULL) fat_close(i);
    }

    int ppid = current->ppid;
    if (ppid >= 0 && ppid < MAX_PCB && pcb[ppid].state == WAIT_CHILD) {
        int any_active = 0;
        for (int i = 0; i < MAX_PCB; i++) {
            if (pcb[i].ppid == ppid && pcb[i].state != UNUSED && pcb[i].state != ZOMBIE) {
                any_active = 1;
                break;
            }
        }
        if (!any_active) pcb[ppid].state = RUNNABLE;
    }
    if (current->pid > 0) {
        uint32_t correct_base = 0x20000 + (current->pid - 1) * SLOT_SIZE;
        if (current->mem_base != correct_base) {
            current->mem_base = correct_base;
            if (ppid >= 0 && ppid < MAX_PCB) pcb[ppid].vfork_count--;
        }
    }
    schedule();
}

int do_wait() {
    int has_child = 0, any_active = 0;
    for (int i = 0; i < MAX_PCB; i++) {
        if (pcb[i].ppid == current->pid && pcb[i].state != UNUSED) {
            has_child = 1;
            if (pcb[i].state != ZOMBIE) { any_active = 1; break; }
        }
    }
    if (!has_child) return -1;
    if (any_active) {
        current->state = WAIT_CHILD;
        current->tf->eip -= 2; // Rewind EIP to 'int 0x80' instruction
        schedule();
        // Return SYS_WAIT so that when the process resumes and re-executes 
        // the syscall (due to EIP rewind), EAX contains the correct syscall ID (9).
        // Returning 0 here would cause it to execute SYS_WRITE (0) instead.
        return SYS_WAIT;
    }
    for (int i = 0; i < MAX_PCB; i++) {
        if (pcb[i].ppid == current->pid && pcb[i].state == ZOMBIE) pcb[i].state = UNUSED;
    }
    return 0;
}

int do_sem_init(int value) {
    for (int i = 0; i < MAX_SEM; i++) {
        if (!semaphores[i].used) {
            semaphores[i].used = 1;
            semaphores[i].value = value;
            semaphores[i].wait_queue = NULL;
            return i;
        }
    }
    return -1;
}

int do_sem_destroy(int sem_id) {
    if (sem_id >= 0 && sem_id < MAX_SEM && semaphores[sem_id].used) {
        semaphores[sem_id].used = 0;
        semaphores[sem_id].wait_queue = NULL;
        return 0;
    }
    return -1;
}

int do_sem_wait(int sem_id) {
    if (sem_id < 0 || sem_id >= MAX_SEM || !semaphores[sem_id].used) return -1;
    
    if (semaphores[sem_id].value > 0) {
        semaphores[sem_id].value--;
        return 0;
    }

    // Block and append to wait queue (FIFO Linked List)
    current->next = NULL;
    if (semaphores[sem_id].wait_queue == NULL) {
        semaphores[sem_id].wait_queue = current;
    } else {
        // Traverse to the end of the list (Tail)
        struct PCB *p = semaphores[sem_id].wait_queue;
        while (p->next != NULL) {
            p = p->next;
        }
        p->next = current;
    }

    current->state = BLOCKED;
    current->tf->eip -= 2; // Rewind EIP to retry syscall later
    schedule();
    // Return SYS_SEM_WAIT (13) to ensure correct syscall re-execution upon wake-up.
    return SYS_SEM_WAIT;
}

int do_sem_post(int sem_id) {
    if (sem_id < 0 || sem_id >= MAX_SEM || !semaphores[sem_id].used) return -1;
    
    semaphores[sem_id].value++;
    
    if (semaphores[sem_id].wait_queue) {
        // FIFO: Wake up the process at the head of the queue
        struct PCB *woken = semaphores[sem_id].wait_queue;
        semaphores[sem_id].wait_queue = woken->next;
        
        woken->state = RUNNABLE;
        woken->next = NULL; // Detach from list
        schedule(); // Fairness: Yield to allow woken process to run immediately
    }
    
    return 0;
}

void do_sleep(int ticks) {
    if (ticks > 0) {
        current->sleep_ticks = ticks;
        current->state = BLOCKED;
    }
    schedule();
}

int do_getpid() { return current->pid; }
int do_getppid() { return current->ppid; }
