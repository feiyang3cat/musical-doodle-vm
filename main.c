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
#include <sys/mman.h>
#include <mach/mach_time.h>
#include <Hypervisor/Hypervisor.h>

/* ============================================================================
 * Constants and Configuration
 * ============================================================================ */

/* Guest memory size: 1MB is plenty for our tiny guest */
#define GUEST_MEM_SIZE      (1 * 1024 * 1024)

/* Guest physical address where we load code */
#define GUEST_CODE_ADDR     0x10000

/* Stack grows down from end of memory */
#define GUEST_STACK_ADDR    (GUEST_MEM_SIZE - 0x1000)

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
     */

    /* Entry point: set up and print hello message */

    /* Print "Hello from TinyVMM guest!\n" using hypercalls */

    /* Load 'H' into x1, hypercall PUTCHAR (x0=1) */
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

    /* ' ' (space) */
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

    /* '!' */
    0xd2800421,     /* mov x1, #'!' (0x21) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* '\n' */
    0xd2800141,     /* mov x1, #'\n' (0x0a) */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Now demonstrate a counter - print some numbers */

    /* Initialize counter in x19 (callee-saved) */
    0xd2800013,     /* mov x19, #0 */

    /* Loop start (offset 48 instructions * 4 = 192 bytes from start) */
    /* loop: */

    /* Print digit: '0' + counter */
    0xd2800601,     /* mov x1, #'0' (0x30) */
    0x8b130021,     /* add x1, x1, x19 */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Print space */
    0xd2800401,     /* mov x1, #' ' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Increment counter */
    0x91000673,     /* add x19, x19, #1 */

    /* Compare with 5 */
    0xf100167f,     /* cmp x19, #5 */

    /* Branch if less than (loop) - go back 9 instructions */
    0x54fffeeb,     /* b.lt loop (branch back 9 instructions = -36 bytes) */

    /* Print newline */
    0xd2800141,     /* mov x1, #'\n' */
    0xd2800020,     /* mov x0, #1 */
    0xd4000002,     /* hvc #0 */

    /* Exit: hypercall 0 */
    0xd2800000,     /* mov x0, #0 (HYPERCALL_EXIT) */
    0xd4000002,     /* hvc #0 */

    /* Infinite loop (should never reach here) */
    0x14000000,     /* b . (branch to self) */
};

/* ============================================================================
 * VMM State
 * ============================================================================ */

typedef struct {
    void *mem;                  /* Guest memory (host virtual address) */
    size_t mem_size;            /* Size of guest memory */
    hv_vcpu_t vcpu;             /* vCPU handle */
    hv_vcpu_exit_t *vcpu_exit;  /* Pointer to exit info structure */
    bool running;               /* Is the VM still running? */
} vm_state_t;

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

/*
 * Initialize the VM: create VM instance and allocate guest memory
 */
static int vm_init(vm_state_t *vm) {
    printf("[VMM] Creating virtual machine...\n");

    /* Step 1: Create the VM instance for this process */
    HV_CHECK(hv_vm_create(NULL));
    printf("[VMM] VM created successfully\n");

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
    printf("[VMM] Allocated %zu KB guest memory at %p\n",
           vm->mem_size / 1024, vm->mem);

    /* Step 3: Map the host memory into guest physical address space
     * The guest will see this memory starting at IPA (Intermediate Physical Address) 0
     */
    HV_CHECK(hv_vm_map(vm->mem, 0, vm->mem_size,
                       HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC));
    printf("[VMM] Mapped guest memory: GPA 0x0 - 0x%zx\n", vm->mem_size);

    return 0;
}

/*
 * Create and configure the vCPU
 */
static int vcpu_init(vm_state_t *vm) {
    printf("[VMM] Creating vCPU...\n");

    /* Create the vCPU
     * The exit pointer will be filled in by the framework
     */
    HV_CHECK(hv_vcpu_create(&vm->vcpu, &vm->vcpu_exit, NULL));
    printf("[VMM] vCPU created with ID: %llu\n", vm->vcpu);

    /* Set up initial register state
     *
     * For ARM64:
     * - PC: Where to start executing
     * - SP: Stack pointer
     * - CPSR: Current program status (we use EL1h = kernel mode with SP_EL1)
     */

    /* Program counter: point to our guest code */
    HV_CHECK(hv_vcpu_set_reg(vm->vcpu, HV_REG_PC, GUEST_CODE_ADDR));

    /* Stack pointer (SP_EL0 is used when running at EL1 with SP_EL0 selected) */
    HV_CHECK(hv_vcpu_set_sys_reg(vm->vcpu, HV_SYS_REG_SP_EL0, GUEST_STACK_ADDR));

    /* CPSR: EL1h mode (bits [3:0] = 0b0101 = EL1 with SP_EL1)
     * Bit 9 (E) = 0: Little endian
     * Bit 8 (A) = 0: No SError masking
     * Bit 7 (I) = 1: IRQ masked (we don't use interrupts)
     * Bit 6 (F) = 1: FIQ masked
     */
    HV_CHECK(hv_vcpu_set_reg(vm->vcpu, HV_REG_CPSR, 0x3c5)); /* EL1h, interrupts masked */

    /* Clear general purpose registers */
    for (int i = 0; i <= 30; i++) {
        HV_CHECK(hv_vcpu_set_reg(vm->vcpu, HV_REG_X0 + i, 0));
    }

    printf("[VMM] vCPU initialized: PC=0x%x, SP=0x%x\n",
           GUEST_CODE_ADDR, GUEST_STACK_ADDR);

    return 0;
}

/*
 * Load guest code into VM memory
 */
static int load_guest(vm_state_t *vm) {
    printf("[VMM] Loading guest code...\n");

    /* Copy guest code to the appropriate location in guest memory */
    size_t code_size = sizeof(guest_code);

    if (GUEST_CODE_ADDR + code_size > vm->mem_size) {
        fprintf(stderr, "Error: Guest code too large\n");
        return -1;
    }

    memcpy((uint8_t *)vm->mem + GUEST_CODE_ADDR, guest_code, code_size);

    printf("[VMM] Loaded %zu bytes of guest code at GPA 0x%x\n",
           code_size, GUEST_CODE_ADDR);

    return 0;
}

/*
 * Handle a hypercall from the guest
 *
 * Hypercalls use the HVC instruction in ARM64. When the guest executes HVC,
 * we get an exception and can read the guest's registers to see what it wants.
 */
static int handle_hypercall(vm_state_t *vm) {
    uint64_t x0, x1, pc;
    // static int call_count = 0;

    /* Read hypercall number and argument */
    hv_vcpu_get_reg(vm->vcpu, HV_REG_X0, &x0);
    hv_vcpu_get_reg(vm->vcpu, HV_REG_X1, &x1);
    hv_vcpu_get_reg(vm->vcpu, HV_REG_PC, &pc);

    /* Debug: show first few hypercalls */
    // if (call_count < 20) {
    //     fprintf(stderr, "[DEBUG] Hypercall #%d: PC=0x%llx x0=%llu x1=0x%llx('%c')\n",
    //             call_count, pc, x0, x1, (x1 >= 32 && x1 < 127) ? (char)x1 : '?');
    // }
    // call_count++;

    switch (x0) {
        case HYPERCALL_EXIT:
            printf("\n[VMM] Guest requested exit\n");
            vm->running = false;
            break;

        case HYPERCALL_PUTCHAR:
            /* Print a single character */
            putchar((int)x1);
            fflush(stdout);
            break;

        case HYPERCALL_PUTS:
            /* Print a string from guest memory */
            {
                if (x1 < vm->mem_size) {
                    const char *str = (const char *)vm->mem + x1;
                    printf("%s", str);
                }
            }
            break;

        default:
            printf("[VMM] Unknown hypercall %llu at PC=0x%llx\n", x0, pc);
            break;
    }

    /* Note: PC already points past the HVC instruction after trap */

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
static int handle_exit(vm_state_t *vm) {
    hv_vcpu_exit_t *exit = vm->vcpu_exit;

    switch (exit->reason) {
        case HV_EXIT_REASON_EXCEPTION: {
            uint32_t ec = ESR_EC(exit->exception.syndrome);
            uint64_t pc;
            hv_vcpu_get_reg(vm->vcpu, HV_REG_PC, &pc);

            switch (ec) {
                case EC_HVC64:
                    /* Hypervisor call - this is our communication channel */
                    return handle_hypercall(vm);

                case EC_SYS64:
                    /* System register access - for now, just skip */
                    printf("[VMM] System register access at PC=0x%llx, skipping\n", pc);
                    hv_vcpu_set_reg(vm->vcpu, HV_REG_PC, pc + 4);
                    break;

                case EC_DABORT_LOWER:
                    printf("[VMM] Data abort at PC=0x%llx, fault addr=0x%llx\n",
                           pc, exit->exception.virtual_address);
                    vm->running = false;
                    return -1;

                case EC_IABORT_LOWER:
                    printf("[VMM] Instruction abort at PC=0x%llx\n", pc);
                    vm->running = false;
                    return -1;

                default:
                    printf("[VMM] Unhandled exception EC=0x%x at PC=0x%llx\n", ec, pc);
                    printf("[VMM] Syndrome=0x%llx\n", exit->exception.syndrome);
                    vm->running = false;
                    return -1;
            }
            break;
        }

        case HV_EXIT_REASON_CANCELED:
            printf("[VMM] vCPU execution canceled\n");
            vm->running = false;
            break;

        case HV_EXIT_REASON_VTIMER_ACTIVATED:
            /* Virtual timer fired - we don't use it, just continue */
            break;

        default:
            printf("[VMM] Unknown exit reason: %d\n", exit->reason);
            vm->running = false;
            return -1;
    }

    return 0;
}

/*
 * Main VM execution loop
 */
static int vm_run(vm_state_t *vm) {
    printf("[VMM] Starting guest execution...\n");
    printf("[VMM] --- Guest Output ---\n");

    vm->running = true;

    while (vm->running) {
        /* Run the vCPU until it exits */
        hv_return_t ret = hv_vcpu_run(vm->vcpu);

        if (ret != HV_SUCCESS) {
            fprintf(stderr, "[VMM] hv_vcpu_run failed: %s\n", hv_strerror(ret));
            return -1;
        }

        /* Handle the exit reason */
        if (handle_exit(vm) < 0) {
            return -1;
        }
    }

    printf("[VMM] --- End Guest Output ---\n");
    return 0;
}

/*
 * Clean up VM resources
 */
static void vm_destroy(vm_state_t *vm) {
    printf("[VMM] Cleaning up...\n");

    if (vm->vcpu) {
        hv_vcpu_destroy(vm->vcpu);
    }

    if (vm->mem) {
        hv_vm_unmap(0, vm->mem_size);
        munmap(vm->mem, vm->mem_size);
    }

    hv_vm_destroy();

    printf("[VMM] VM destroyed\n");
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(void) {
    vm_state_t vm = {0};
    int result = 0;

    printf("╔════════════════════════════════════════╗\n");
    printf("║   TinyVMM - macOS Hypervisor Demo      ║\n");
    printf("║   A minimal VMM for learning           ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    /* Initialize the VM */
    if (vm_init(&vm) < 0) {
        fprintf(stderr, "\nFailed to initialize VM.\n");
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
        printf("\n[VMM] Guest completed successfully!\n");
    }

    return result < 0 ? 1 : 0;
}
