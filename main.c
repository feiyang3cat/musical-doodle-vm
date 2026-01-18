/*
 * TinyVMM - A minimal Virtual Machine Monitor for macOS
 *
 * This is an educational project demonstrating how to use Apple's
 * Hypervisor.framework to create a simple VM on Apple Silicon.
 *
 * Think of it as a "Hello World" for hypervisors - the simplest possible
 * VMM that actually runs guest code.
 *
 * Key concepts demonstrated:
 * 1. VM creation and destruction
 * 2. Memory mapping (guest physical address space)
 * 3. vCPU creation and register setup
 * 4. Running guest code and handling VM exits
 * 5. Hypercall interface for guest-host communication
 *
 * Build: make
 * Run:   ./tinyvmm (or: make run)
 *
 * Note: Requires macOS 11.0+ on Apple Silicon
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <mach/mach_time.h>
#include <Hypervisor/Hypervisor.h>

/* ============================================================================
 * Constants and Configuration
 * ============================================================================ */

/* Guest memory size: 1MB is plenty for our tiny guest */
#define GUEST_MEM_SIZE      (1 * 1024 * 1024)

/* Maximum number of vCPUs per VM */
#define MAX_VCPUS           2

/* Guest physical address where we load code */
#define GUEST_CODE_ADDR     0x10000

/* Second entry point for VM2's second vCPU (offset from GUEST_CODE_ADDR) */
#define GUEST_CODE2_OFFSET  0x1000

/* Stack grows down from end of memory (each vCPU gets its own stack area) */
#define GUEST_STACK_ADDR    (GUEST_MEM_SIZE - 0x1000)
#define GUEST_STACK2_ADDR   (GUEST_MEM_SIZE - 0x2000)

/* ARM64 Exception Syndrome Register (ESR) bit field extraction */
#define ESR_EC_SHIFT        26
#define ESR_EC_MASK         0x3F
#define ESR_EC(esr)         (((esr) >> ESR_EC_SHIFT) & ESR_EC_MASK)

/* Exception Class (EC) values we care about */
#define EC_HVC64            0x16    /* HVC instruction (AArch64) */
#define EC_SMC64            0x17    /* SMC instruction (AArch64) */
#define EC_SYS64            0x18    /* MSR/MRS or System instruction */
#define EC_DABORT_LOWER     0x24    /* Data abort from lower EL */
#define EC_IABORT_LOWER     0x20    /* Instruction abort from lower EL */

/* Hypercall numbers (our simple guest-host interface) */
#define HYPERCALL_EXIT      0       /* Guest wants to exit */
#define HYPERCALL_PUTCHAR   1       /* Print a character */
#define HYPERCALL_PUTS      2       /* Print a string (address in x1) */

/* ============================================================================
 * Guest Code
 * ============================================================================
 *
 * This is a minimal ARM64 program that runs inside our VM.
 * It demonstrates:
 * - Using HVC (hypervisor call) for guest-host communication
 * - Printing characters and strings
 * - Clean exit
 *
 * The guest runs at EL1 (kernel mode) in the VM.
 */

static const uint32_t guest_code[] = {
    /*
     * ARM64 instructions (little-endian)
     * Each instruction is 4 bytes
     *
     * Input: X20 contains VM ID (1 or 2), set by VMM before execution
     */

    /* Print "Hello from VM " */

    /* 'H' */
    0xd2800901,     /* mov x1, #'H' (0x48) */
    0xd2800020,     /* mov x0, #1 (HYPERCALL_PUTCHAR) */
    0xd4000002,     /* hvc #0 */

    /* 'e' */
    0xd2800ca1,     /* mov x1, #'e' (0x65) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* 'l' */
    0xd2800d81,     /* mov x1, #'l' (0x6c) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* 'l' */
    0xd2800d81,     /* mov x1, #'l' (0x6c) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* 'o' */
    0xd2800de1,     /* mov x1, #'o' (0x6f) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* ' ' */
    0xd2800401,     /* mov x1, #' ' (0x20) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* 'f' */
    0xd2800cc1,     /* mov x1, #'f' (0x66) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* 'r' */
    0xd2800e41,     /* mov x1, #'r' (0x72) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* 'o' */
    0xd2800de1,     /* mov x1, #'o' (0x6f) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* 'm' */
    0xd2800da1,     /* mov x1, #'m' (0x6d) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* ' ' */
    0xd2800401,     /* mov x1, #' ' (0x20) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* 'V' */
    0xd2800ac1,     /* mov x1, #'V' (0x56) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* 'M' */
    0xd28009a1,     /* mov x1, #'M' (0x4d) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* ' ' */
    0xd2800401,     /* mov x1, #' ' (0x20) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Print VM ID from X20: '0' + X20 */
    0xd2800601,     /* mov x1, #'0' (0x30) */
    0x8b140021,     /* add x1, x1, x20 */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* '!' */
    0xd2800421,     /* mov x1, #'!' (0x21) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* '\n' */
    0xd2800141,     /* mov x1, #'\n' (0x0a) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Counter loop: print "VM N: 0 1 2 3 4" */

    /* Print "VM " */
    0xd2800ac1,     /* mov x1, #'V' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd28009a1,     /* mov x1, #'M' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800401,     /* mov x1, #' ' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Print VM ID */
    0xd2800601,     /* mov x1, #'0' */
    0x8b140021,     /* add x1, x1, x20 */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Print ": " */
    0xd2800741,     /* mov x1, #':' (0x3a) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800401,     /* mov x1, #' ' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Initialize counter in x19 */
    0xd2800013,     /* mov x19, #0 */

    /* loop: Print digit */
    0xd2800601,     /* mov x1, #'0' (0x30) */
    0x8b130021,     /* add x1, x1, x19 */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Print space */
    0xd2800401,     /* mov x1, #' ' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Increment and compare */
    0x91000673,     /* add x19, x19, #1 */
    0xf100167f,     /* cmp x19, #5 */
    0x54fffeeb,     /* b.lt loop (-36 bytes, back 9 instructions) */

    /* Print newline */
    0xd2800141,     /* mov x1, #'\n' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Exit */
    0xd2800000,     /* mov x0, #0 (HYPERCALL_EXIT) */
    0xd4000002,     /* hvc #0 */

    /* Infinite loop (should never reach) */
    0x14000000,     /* b . */
};

/*
 * Guest code for VM 2: Dual vCPU parallel computation
 *
 * vCPU 0 entry: Computes sum of even numbers (0+2+4+6+8 = 20)
 * vCPU 1 entry: Computes sum of odd numbers (1+3+5+7+9 = 25)
 *
 * Input registers (set by VMM):
 *   X20 = VM ID (always 2)
 *   X21 = vCPU ID (0 or 1)
 */
static const uint32_t guest_code_vm2_vcpu0[] = {
    /*
     * vCPU 0: Sum even numbers and print result
     * Computes: 0 + 2 + 4 + 6 + 8 = 20
     */

    /* Print "vCPU 0: " */
    0xd2800ec1,     /* mov x1, #'v' (0x76) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800c21,     /* mov x1, #'C' (0x43) - using 'C' for CPU */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800a01,     /* mov x1, #'P' (0x50) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800a81,     /* mov x1, #'U' (0x55) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800401,     /* mov x1, #' ' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800601,     /* mov x1, #'0' - vCPU ID */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800741,     /* mov x1, #':' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800401,     /* mov x1, #' ' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Print "even sum = " */
    0xd2800ca1,     /* mov x1, #'e' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800ec1,     /* mov x1, #'v' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800ca1,     /* mov x1, #'e' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800dc1,     /* mov x1, #'n' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800401,     /* mov x1, #' ' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Compute sum: 0+2+4+6+8 */
    0xd2800013,     /* mov x19, #0 (sum) */
    0xd2800014,     /* mov x20, #0 (counter, reusing x20) */

    /* loop: add counter to sum, increment by 2 */
    0x8b140273,     /* add x19, x19, x20 */
    0x91000a94,     /* add x20, x20, #2 */
    0xf100291f,     /* cmp x20, #10 */
    0x54ffffab,     /* b.lt loop (-3 instructions) */

    /* Print result (20 = '2' '0') */
    0xd2800441,     /* mov x1, #'2' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800601,     /* mov x1, #'0' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800141,     /* mov x1, #'\n' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Exit */
    0xd2800000,     /* mov x0, #0 */
    0xd4000002,     /* hvc #0 */

    0x14000000,     /* b . */
};

static const uint32_t guest_code_vm2_vcpu1[] = {
    /*
     * vCPU 1: Sum odd numbers and print result
     * Computes: 1 + 3 + 5 + 7 + 9 = 25
     */

    /* Print "vCPU 1: " */
    0xd2800ec1,     /* mov x1, #'v' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800c21,     /* mov x1, #'C' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800a01,     /* mov x1, #'P' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800a81,     /* mov x1, #'U' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800401,     /* mov x1, #' ' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800621,     /* mov x1, #'1' - vCPU ID */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800741,     /* mov x1, #':' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800401,     /* mov x1, #' ' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Print "odd sum = " */
    0xd2800de1,     /* mov x1, #'o' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800c81,     /* mov x1, #'d' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800c81,     /* mov x1, #'d' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800401,     /* mov x1, #' ' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Compute sum: 1+3+5+7+9 */
    0xd2800013,     /* mov x19, #0 (sum) */
    0xd2800034,     /* mov x20, #1 (counter starts at 1) */

    /* loop: add counter to sum, increment by 2 */
    0x8b140273,     /* add x19, x19, x20 */
    0x91000a94,     /* add x20, x20, #2 */
    0xf100291f,     /* cmp x20, #10 */
    0x54ffffcb,     /* b.lt loop */

    /* Print result (25 = '2' '5') */
    0xd2800441,     /* mov x1, #'2' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd28006a1,     /* mov x1, #'5' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    0xd2800141,     /* mov x1, #'\n' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Exit */
    0xd2800000,     /* mov x0, #0 */
    0xd4000002,     /* hvc #0 */

    0x14000000,     /* b . */
};

/* ============================================================================
 * VMM State
 * ============================================================================ */

typedef struct {
    int id;                     /* VM identifier (1 or 2) */
    int num_vcpus;              /* Number of vCPUs in this VM */
    void *mem;                  /* Guest memory (host virtual address) */
    size_t mem_size;            /* Size of guest memory */
    hv_vcpu_t vcpus[MAX_VCPUS];             /* vCPU handles */
    hv_vcpu_exit_t *vcpu_exits[MAX_VCPUS];  /* Pointers to exit info structures */
    bool running;               /* Is the VM still running? */
    pthread_mutex_t lock;       /* Lock for shared state */
} vm_state_t;

/* Per-vCPU thread context */
typedef struct {
    vm_state_t *vm;
    int vcpu_idx;               /* Index into vcpus array */
} vcpu_thread_ctx_t;

/* ============================================================================
 * Error Handling
 * ============================================================================ */

static const char *hv_strerror(hv_return_t ret) {
    switch (ret) {
        case HV_SUCCESS:        return "Success";
        case HV_ERROR:          return "Error";
        case HV_BUSY:           return "Busy";
        case HV_BAD_ARGUMENT:   return "Bad argument";
        case HV_NO_RESOURCES:   return "No resources";
        case HV_NO_DEVICE:      return "No device";
        case HV_DENIED:         return "Denied (missing entitlement?)";
        case HV_UNSUPPORTED:    return "Unsupported";
        default:                return "Unknown error";
    }
}

#define HV_CHECK(call) do { \
    hv_return_t _ret = (call); \
    if (_ret != HV_SUCCESS) { \
        fprintf(stderr, "Error: %s failed: %s (0x%x)\n", \
                #call, hv_strerror(_ret), _ret); \
        return -1; \
    } \
} while(0)

/* ============================================================================
 * VM Lifecycle Functions
 * ============================================================================ */

/* Forward declaration */
static int vcpu_init_single(vm_state_t *vm, int vcpu_idx);

/*
 * Initialize the VM: create VM instance and allocate guest memory
 */
static int vm_init(vm_state_t *vm) {
    printf("[VM %d] Creating virtual machine...\n", vm->id);

    /* Step 1: Create the VM instance for this process */
    HV_CHECK(hv_vm_create(NULL));
    printf("[VM %d] VM created successfully\n", vm->id);

    /* Step 2: Allocate guest memory
     * We use mmap to get page-aligned memory that can be mapped into the guest
     */
    vm->mem_size = GUEST_MEM_SIZE;
    vm->mem = mmap(NULL, vm->mem_size,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (vm->mem == MAP_FAILED) {
        perror("mmap");
        hv_vm_destroy();
        return -1;
    }
    printf("[VM %d] Allocated %zu KB guest memory at %p\n",
           vm->id, vm->mem_size / 1024, vm->mem);

    /* Step 3: Map the host memory into guest physical address space
     * The guest will see this memory starting at IPA (Intermediate Physical Address) 0
     */
    HV_CHECK(hv_vm_map(vm->mem, 0, vm->mem_size,
                       HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC));
    printf("[VM %d] Mapped guest memory: GPA 0x0 - 0x%zx\n", vm->id, vm->mem_size);

    return 0;
}

/*
 * Create and configure vCPUs
 *
 * For single-vCPU VMs: creates the vCPU here
 * For multi-vCPU VMs: vCPUs are created in their own threads (required by Hypervisor.framework)
 */
static int vcpu_init(vm_state_t *vm) {
    printf("[VM %d] Creating %d vCPU(s)...\n", vm->id, vm->num_vcpus);

    /* Initialize mutex for multi-vCPU synchronization */
    pthread_mutex_init(&vm->lock, NULL);

    if (vm->num_vcpus == 1) {
        /* Single vCPU: create it here in the main thread */
        if (vcpu_init_single(vm, 0) < 0) {
            return -1;
        }
    }
    /* Multi-vCPU: vCPUs will be created in vcpu_thread_func */

    return 0;
}

/*
 * Load guest code into VM memory
 */
static int load_guest(vm_state_t *vm) {
    printf("[VM %d] Loading guest code...\n", vm->id);

    if (vm->id == 1) {
        /* VM 1: Single vCPU with simple hello world */
        size_t code_size = sizeof(guest_code);

        if (GUEST_CODE_ADDR + code_size > vm->mem_size) {
            fprintf(stderr, "[VM %d] Error: Guest code too large\n", vm->id);
            return -1;
        }

        memcpy((uint8_t *)vm->mem + GUEST_CODE_ADDR, guest_code, code_size);
        printf("[VM %d] Loaded %zu bytes at GPA 0x%x (1 vCPU)\n",
               vm->id, code_size, GUEST_CODE_ADDR);
    } else {
        /* VM 2: Two vCPUs with parallel computation */
        size_t code0_size = sizeof(guest_code_vm2_vcpu0);
        size_t code1_size = sizeof(guest_code_vm2_vcpu1);

        /* Load vCPU 0 code at GUEST_CODE_ADDR */
        memcpy((uint8_t *)vm->mem + GUEST_CODE_ADDR,
               guest_code_vm2_vcpu0, code0_size);
        printf("[VM %d] Loaded %zu bytes at GPA 0x%x (vCPU 0: even sum)\n",
               vm->id, code0_size, GUEST_CODE_ADDR);

        /* Load vCPU 1 code at GUEST_CODE_ADDR + GUEST_CODE2_OFFSET */
        memcpy((uint8_t *)vm->mem + GUEST_CODE_ADDR + GUEST_CODE2_OFFSET,
               guest_code_vm2_vcpu1, code1_size);
        printf("[VM %d] Loaded %zu bytes at GPA 0x%x (vCPU 1: odd sum)\n",
               vm->id, code1_size, GUEST_CODE_ADDR + GUEST_CODE2_OFFSET);
    }

    return 0;
}

/*
 * Handle a hypercall from the guest
 *
 * Hypercalls use the HVC instruction in ARM64. When the guest executes HVC,
 * we get an exception and can read the guest's registers to see what it wants.
 */
static int handle_hypercall(vm_state_t *vm, int vcpu_idx) {
    uint64_t x0, x1, pc;
    hv_vcpu_t vcpu = vm->vcpus[vcpu_idx];

    /* Read hypercall number and argument */
    hv_vcpu_get_reg(vcpu, HV_REG_X0, &x0);
    hv_vcpu_get_reg(vcpu, HV_REG_X1, &x1);
    hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc);

    switch (x0) {
        case HYPERCALL_EXIT:
            return 1;  /* Signal this vCPU should stop */

        case HYPERCALL_PUTCHAR:
            /* Print a single character (lock for clean output) */
            pthread_mutex_lock(&vm->lock);
            putchar((int)x1);
            fflush(stdout);
            pthread_mutex_unlock(&vm->lock);
            break;

        case HYPERCALL_PUTS:
            /* Print a string from guest memory */
            pthread_mutex_lock(&vm->lock);
            if (x1 < vm->mem_size) {
                const char *str = (const char *)vm->mem + x1;
                printf("%s", str);
            }
            pthread_mutex_unlock(&vm->lock);
            break;

        default:
            printf("[VM %d vCPU %d] Unknown hypercall %llu at PC=0x%llx\n",
                   vm->id, vcpu_idx, x0, pc);
            break;
    }

    return 0;
}

/*
 * Handle a VM exit
 *
 * This is called whenever the guest stops executing and control returns
 * to the VMM. Common reasons include:
 * - Hypercalls (HVC instruction)
 * - System register access we don't allow
 * - Memory access faults
 * - Interrupts
 */
static int handle_exit(vm_state_t *vm, int vcpu_idx) {
    hv_vcpu_exit_t *exit = vm->vcpu_exits[vcpu_idx];
    hv_vcpu_t vcpu = vm->vcpus[vcpu_idx];

    switch (exit->reason) {
        case HV_EXIT_REASON_EXCEPTION: {
            uint32_t ec = ESR_EC(exit->exception.syndrome);
            uint64_t pc;
            hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc);

            switch (ec) {
                case EC_HVC64:
                    /* Hypervisor call - this is our communication channel */
                    return handle_hypercall(vm, vcpu_idx);

                case EC_SYS64:
                    /* System register access - for now, just skip */
                    printf("[VM %d vCPU %d] System register access at PC=0x%llx, skipping\n",
                           vm->id, vcpu_idx, pc);
                    hv_vcpu_set_reg(vcpu, HV_REG_PC, pc + 4);
                    break;

                case EC_DABORT_LOWER:
                    printf("[VM %d vCPU %d] Data abort at PC=0x%llx, fault addr=0x%llx\n",
                           vm->id, vcpu_idx, pc, exit->exception.virtual_address);
                    return -1;

                case EC_IABORT_LOWER:
                    printf("[VM %d vCPU %d] Instruction abort at PC=0x%llx\n",
                           vm->id, vcpu_idx, pc);
                    return -1;

                default:
                    printf("[VM %d vCPU %d] Unhandled exception EC=0x%x at PC=0x%llx\n",
                           vm->id, vcpu_idx, ec, pc);
                    printf("[VM %d vCPU %d] Syndrome=0x%llx\n",
                           vm->id, vcpu_idx, exit->exception.syndrome);
                    return -1;
            }
            break;
        }

        case HV_EXIT_REASON_CANCELED:
            printf("[VM %d vCPU %d] vCPU execution canceled\n", vm->id, vcpu_idx);
            return 1;

        case HV_EXIT_REASON_VTIMER_ACTIVATED:
            /* Virtual timer fired - we don't use it, just continue */
            break;

        default:
            printf("[VM %d vCPU %d] Unknown exit reason: %d\n", vm->id, vcpu_idx, exit->reason);
            return -1;
    }

    return 0;
}

/*
 * Initialize a single vCPU (must be called from the thread that will run it)
 */
static int vcpu_init_single(vm_state_t *vm, int vcpu_idx) {
    /* Create the vCPU in this thread */
    hv_return_t ret = hv_vcpu_create(&vm->vcpus[vcpu_idx], &vm->vcpu_exits[vcpu_idx], NULL);
    if (ret != HV_SUCCESS) {
        fprintf(stderr, "[VM %d vCPU %d] hv_vcpu_create failed: %s\n",
                vm->id, vcpu_idx, hv_strerror(ret));
        return -1;
    }

    hv_vcpu_t vcpu = vm->vcpus[vcpu_idx];

    /* Determine entry point and stack for this vCPU */
    uint64_t pc_addr, sp_addr;
    if (vm->id == 2) {
        pc_addr = GUEST_CODE_ADDR + (vcpu_idx * GUEST_CODE2_OFFSET);
        sp_addr = (vcpu_idx == 0) ? GUEST_STACK_ADDR : GUEST_STACK2_ADDR;
    } else {
        pc_addr = GUEST_CODE_ADDR;
        sp_addr = GUEST_STACK_ADDR;
    }

    /* Program counter */
    hv_vcpu_set_reg(vcpu, HV_REG_PC, pc_addr);
    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL0, sp_addr);
    hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0x3c5);

    /* Clear and set registers */
    for (int r = 0; r <= 30; r++) {
        hv_vcpu_set_reg(vcpu, HV_REG_X0 + r, 0);
    }
    hv_vcpu_set_reg(vcpu, HV_REG_X20, vm->id);
    hv_vcpu_set_reg(vcpu, HV_REG_X21, vcpu_idx);

    printf("[VM %d] vCPU %d initialized: PC=0x%llx, SP=0x%llx\n",
           vm->id, vcpu_idx, pc_addr, sp_addr);

    return 0;
}

/*
 * vCPU thread function - creates and runs a single vCPU in its own thread
 */
static void *vcpu_thread_func(void *arg) {
    vcpu_thread_ctx_t *ctx = (vcpu_thread_ctx_t *)arg;
    vm_state_t *vm = ctx->vm;
    int vcpu_idx = ctx->vcpu_idx;

    /* Create vCPU in this thread (required by Hypervisor.framework) */
    if (vcpu_init_single(vm, vcpu_idx) < 0) {
        return (void *)-1;
    }

    hv_vcpu_t vcpu = vm->vcpus[vcpu_idx];

    while (1) {
        /* Run the vCPU until it exits */
        hv_return_t ret = hv_vcpu_run(vcpu);

        if (ret != HV_SUCCESS) {
            fprintf(stderr, "[VM %d vCPU %d] hv_vcpu_run failed: %s\n",
                    vm->id, vcpu_idx, hv_strerror(ret));
            return (void *)-1;
        }

        /* Handle the exit reason */
        int result = handle_exit(vm, vcpu_idx);
        if (result != 0) {
            /* Exit requested or error */
            break;
        }
    }

    return NULL;
}

/*
 * Main VM execution loop
 */
static int vm_run(vm_state_t *vm) {
    printf("[VM %d] Starting guest execution (%d vCPU%s)...\n",
           vm->id, vm->num_vcpus, vm->num_vcpus > 1 ? "s" : "");
    printf("[VM %d] --- Guest Output ---\n", vm->id);

    vm->running = true;

    if (vm->num_vcpus == 1) {
        /* Single vCPU: run directly in this thread */
        hv_vcpu_t vcpu = vm->vcpus[0];

        while (vm->running) {
            hv_return_t ret = hv_vcpu_run(vcpu);

            if (ret != HV_SUCCESS) {
                fprintf(stderr, "[VM %d] hv_vcpu_run failed: %s\n", vm->id, hv_strerror(ret));
                return -1;
            }

            int result = handle_exit(vm, 0);
            if (result != 0) {
                vm->running = false;
            }
        }
    } else {
        /* Multiple vCPUs: run each in its own thread */
        pthread_t threads[MAX_VCPUS];
        vcpu_thread_ctx_t contexts[MAX_VCPUS];

        for (int i = 0; i < vm->num_vcpus; i++) {
            contexts[i].vm = vm;
            contexts[i].vcpu_idx = i;
            pthread_create(&threads[i], NULL, vcpu_thread_func, &contexts[i]);
        }

        /* Wait for all vCPU threads to complete */
        for (int i = 0; i < vm->num_vcpus; i++) {
            pthread_join(threads[i], NULL);
        }

        vm->running = false;
    }

    printf("[VM %d] --- End Guest Output ---\n", vm->id);
    return 0;
}

/*
 * Clean up VM resources
 */
static void vm_destroy(vm_state_t *vm) {
    printf("[VM %d] Cleaning up...\n", vm->id);

    for (int i = 0; i < vm->num_vcpus; i++) {
        if (vm->vcpus[i]) {
            hv_vcpu_destroy(vm->vcpus[i]);
        }
    }

    if (vm->mem) {
        hv_vm_unmap(0, vm->mem_size);
        munmap(vm->mem, vm->mem_size);
    }

    pthread_mutex_destroy(&vm->lock);
    hv_vm_destroy();

    printf("[VM %d] VM destroyed\n", vm->id);
}

/* ============================================================================
 * Single VM Runner (called in child process)
 * ============================================================================ */

static int run_single_vm(int vm_id) {
    vm_state_t vm = {0};
    vm.id = vm_id;
    vm.num_vcpus = (vm_id == 1) ? 1 : 2;  /* VM 1: 1 vCPU, VM 2: 2 vCPUs */
    int result = 0;

    /* Initialize the VM */
    if (vm_init(&vm) < 0) {
        fprintf(stderr, "\n[VM %d] Failed to initialize VM.\n", vm_id);
        fprintf(stderr, "Make sure you have the hypervisor entitlement.\n");
        fprintf(stderr, "For development, run: codesign --entitlements entitlements.plist -s - tinyvmm\n");
        return 1;
    }

    /* Create vCPU */
    if (vcpu_init(&vm) < 0) {
        vm_destroy(&vm);
        return 1;
    }

    /* Load guest code */
    if (load_guest(&vm) < 0) {
        vm_destroy(&vm);
        return 1;
    }

    /* Run the VM */
    result = vm_run(&vm);

    /* Clean up */
    vm_destroy(&vm);

    if (result == 0) {
        printf("\n[VM %d] Guest completed successfully!\n", vm_id);
    }

    return result < 0 ? 1 : 0;
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(void) {
    pid_t pid1, pid2;
    int status1, status2;

    printf("╔════════════════════════════════════════╗\n");
    printf("║   TinyVMM - macOS Hypervisor Demo      ║\n");
    printf("║   Running 2 VMs in parallel            ║\n");
    printf("╚════════════════════════════════════════╝\n\n");
    fflush(stdout);

    /*
     * Fork two child processes, each running its own VM.
     * Apple's Hypervisor.framework allows one VM per process,
     * so we use separate processes for true isolation.
     */

    /* Fork first child for VM 1 */
    pid1 = fork();
    if (pid1 < 0) {
        perror("fork");
        return 1;
    }
    if (pid1 == 0) {
        /* Child process 1: Run VM 1 */
        return run_single_vm(1);
    }

    /* Fork second child for VM 2 */
    pid2 = fork();
    if (pid2 < 0) {
        perror("fork");
        /* Kill first child if second fork fails */
        kill(pid1, SIGTERM);
        waitpid(pid1, NULL, 0);
        return 1;
    }
    if (pid2 == 0) {
        /* Child process 2: Run VM 2 */
        return run_single_vm(2);
    }

    /* Parent process: Wait for both VMs to complete */
    printf("[Parent] Started VM 1 (PID %d) and VM 2 (PID %d)\n", pid1, pid2);
    printf("[Parent] Waiting for VMs to complete...\n\n");

    waitpid(pid1, &status1, 0);
    waitpid(pid2, &status2, 0);

    printf("\n[Parent] Both VMs finished.\n");

    /* Return success only if both VMs succeeded */
    if (WIFEXITED(status1) && WEXITSTATUS(status1) == 0 &&
        WIFEXITED(status2) && WEXITSTATUS(status2) == 0) {
        printf("[Parent] All VMs completed successfully!\n");
        return 0;
    } else {
        printf("[Parent] One or more VMs failed.\n");
        return 1;
    }
}
