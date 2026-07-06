#include "proc.h"
#include "device.h"
#include "syscall.h"

struct PCB pcb[MAX_PCB];
struct PCB *current = NULL;
struct Semaphore semaphores[MAX_SEM];

// --- Process Scheduling (Round-Robin) ---
void schedule() {
    struct PCB *next = NULL;
    // TODO 1: 扫描策略改为时间片轮转
    int start = (current->pid + 1) % MAX_PCB;
    for (int i = 0; i < MAX_PCB; i++) {
        int idx = (start + i) % MAX_PCB;
        if (idx == 0) continue;
        if (pcb[idx].state == RUNNABLE || pcb[idx].state == RUNNING) {
            next = &pcb[idx];
            break;
        }
    }

    // TODO 2: Idle进程低优先级处理
    if (!next) {
        if (pcb[0].state == RUNNABLE || pcb[0].state == RUNNING) {
            next = &pcb[0];
        }
    }

    // TODO 3: 上下文切换与时间片重置
    if (!next) return;
    if (next == current) {
        current->state = RUNNING;
        current->time_count = 5;
        return;
    }

    if (current->state == RUNNING) current->state = RUNNABLE;
    next->state = RUNNING;
    next->time_count = 5;
    current = next;

    extern TSS tss;
    tss.esp0 = (uint32_t)current->kstack + KSTACK_SIZE;
    update_user_seg_base(current->mem_base, current->mem_limit);
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
    uint32_t stack_len = SEG_LIMIT - parent->tf->esp_user;
    if (stack_len > 0 && stack_len < VFORK_STACK_OFFSET) {
        uint32_t *src_stack = (uint32_t *)(parent->mem_base + parent->tf->esp_user);
        uint32_t *dst_stack = (uint32_t *)(child->mem_base + child->tf->esp_user);
        
        for (uint32_t i = 0; i < stack_len / 4; i++) {
            uint32_t val = src_stack[i];
            if (val >= parent->tf->esp_user && val < SEG_LIMIT) {
                dst_stack[i] = val - VFORK_STACK_OFFSET;
            } else {
                dst_stack[i] = val;
            }
        }
        
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
    child->next = NULL;

    schedule();
    return child->pid;
}

void do_exec(uint32_t sec, uint32_t num) {
    if (current->pid > 0) {
        uint32_t correct_base = 0x20000 + (current->pid - 1) * SLOT_SIZE;
        if (current->mem_base != correct_base) {
            current->mem_base = correct_base;
            if (current->ppid >= 0 && current->ppid < MAX_PCB) pcb[current->ppid].vfork_count--;
        }
    }
    for (uint32_t i = 0; i < num; i++) readSect((void *)(current->mem_base + i * 512), sec + i);
    current->mem_limit = SEG_LIMIT;
    update_user_seg_base(current->mem_base, current->mem_limit);
    current->tf->eip = 0;
    current->tf->esp_user = SEG_LIMIT + 1; 
    current->tf->eax = 0;
}

void do_exit() {
/*TODO: 实现完整的 exit 功能，处理两种情况
- 情况1：父进程正在等待（state == WAIT_CHILD）
  → 将当前进程设为 ZOMBIE，等待父进程调用 wait 回收
- 情况2：父进程不存在或不在等待
  → 将当前进程设为 UNUSED，自己释放 PCB 槽位

实现步骤：
1. 将当前进程 state 设为 ZOMBIE
2. 遍历所有子进程，将它们的 ppid 设为 0（孤儿进程交给 idle 收养）
3. 检查父进程是否在 WAIT_CHILD 状态
4. 若父进程在等待且当前进程是最后一个活跃子进程，唤醒父进程
5. vfork 相关内存恢复（如有）
6. 【新增】检查父进程是否存在且在等待子进程
   - 若父进程不存在（ppid < 0）或父进程状态不是 WAIT_CHILD
   - 将当前进程 state 改为 UNUSED（自行回收）
7. 调用 schedule()
*/
    current->state = ZOMBIE;

    for (int i = 0; i < MAX_PCB; i++) {
        if (pcb[i].ppid == current->pid) pcb[i].ppid = 0;
    }

    struct PCB *parent = NULL;
    if (current->ppid >= 0 && current->ppid < MAX_PCB) {
        parent = &pcb[current->ppid];
    }

    if (parent && parent->state == WAIT_CHILD) {
        int has_active = 0;
        for (int i = 0; i < MAX_PCB; i++) {
            if (pcb[i].ppid == parent->pid && pcb[i].state != UNUSED && pcb[i].state != ZOMBIE) {
                has_active = 1;
                break;
            }
        }
        if (!has_active) parent->state = RUNNABLE;
    }

    if (current->pid > 0) {
        uint32_t correct_base = 0x20000 + (current->pid - 1) * SLOT_SIZE;
        if (current->mem_base != correct_base) {
            current->mem_base = correct_base;
            if (parent) parent->vfork_count--;
        }
    }

    if (!parent || parent->state != WAIT_CHILD) {
        current->state = UNUSED;
    }

    schedule();
}

int do_wait() {
    // TODO 1: 子进程存在性检查
    int has_child = 0;
    for (int i = 0; i < MAX_PCB; i++) {
        if (pcb[i].ppid == current->pid && pcb[i].state != UNUSED) {
            has_child = 1;
            break;
        }
    }
    if (!has_child) return -1;

    // TODO 2: 活跃子进程检查
    for (int i = 0; i < MAX_PCB; i++) {
        if (pcb[i].ppid == current->pid && pcb[i].state != UNUSED && pcb[i].state != ZOMBIE) {
            current->state = WAIT_CHILD;
            current->tf->eip -= 2;
            schedule();
            return SYS_WAIT;
        }
    }

    // TODO 3: 僵尸进程清理
    for (int i = 0; i < MAX_PCB; i++) {
        if (pcb[i].ppid == current->pid && pcb[i].state == ZOMBIE) {
            pcb[i].state = UNUSED;
        }
    }
    return 0;
}

int do_sem_init(int value) {
    // TODO 1: sem_init实现
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
    // TODO 2: sem_destroy实现
    if (sem_id < 0 || sem_id >= MAX_SEM) return -1;
    if (!semaphores[sem_id].used) return -1;

    semaphores[sem_id].used = 0;
    semaphores[sem_id].wait_queue = NULL;
    return 0;
}

int do_sem_wait(int sem_id) {
    // TODO 1: 参数校验
    if (sem_id < 0 || sem_id >= MAX_SEM) return -1;
    if (!semaphores[sem_id].used) return -1;

    // TODO 2: 资源检查与获取
    if (semaphores[sem_id].value > 0) {
        semaphores[sem_id].value--;
        return 0;
    }

    // TODO 3: 阻塞与入队
    current->next = NULL;
    if (!semaphores[sem_id].wait_queue) {
        semaphores[sem_id].wait_queue = current;
    } else {
        struct PCB *tail = semaphores[sem_id].wait_queue;
        while (tail->next) tail = tail->next;
        tail->next = current;
    }
    current->state = BLOCKED;
    current->tf->eip -= 2;
    schedule();
    return SYS_SEM_WAIT;
}

int do_sem_post(int sem_id) {
    // TODO 1: 参数校验
    if (sem_id < 0 || sem_id >= MAX_SEM) return -1;
    if (!semaphores[sem_id].used) return -1;

    // TODO 2: 值递增
    semaphores[sem_id].value++;

    // TODO 3: 唤醒等待进程
    if (semaphores[sem_id].wait_queue) {
        struct PCB *woken = semaphores[sem_id].wait_queue;
        semaphores[sem_id].wait_queue = woken->next;
        woken->next = NULL;
        woken->state = RUNNABLE;
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
