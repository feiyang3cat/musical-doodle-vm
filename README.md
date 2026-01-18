# TinyVMM - A Minimal Hypervisor for macOS

A "Hello World" Virtual Machine Monitor (VMM) exploring how to create a tiny sandbox on macOS using Apple's Hypervisor.framework.


## Context Knowledge

### basic terms

**hodgepodge of terms**:
Hypervisor, VMM, VM, GuestOS, BareMetal, MicroKernel, UniKernel, Monolithic Kernel, Hybrid Kernel
Hypercalls, Systemcalls, Traps/Interrupts/Exceptions, Privilege Levels(mode/ring/exception level)

**stacks**:
| Traditional Stack                       | Virtualization Stack                              |
| --------------------------------------- | ------------------------------------------------- |
| Hardware                                | Hardware                                          |
| OS (Kernel, e.g. Linux, macOS, Windows) | └── Hypervisor + VMM (often used interchangeably) |
| Application                             | └── Virtual Machine (VM)                          |
|                                         | └── Guest OS (e.g. Linux, Windows, etc.)          |
| --------------------------------------- | ------------------------------------------------- |
| Application                             | Application                                       |
| ↓ syscall (trap to kernel)              | ↓ syscall (trap to kernel)                        |
| OS / Kernel                             | Kernel (e.g. Guest OS)                            |
| ↓ (directly on hardware)                | ↓ hypercall (trap to hypervisor)                  |
| Hardware (CPU / Memory / Devices)       | Hypervisor                                        |
|                                         | ↓ hardware instructions                           |
|                                         | CPU / Memory / Devices                            |

### basic problems to solve

The management of multiple OSes in the same machine:
- thread related
- memory management
- IO device management (a lot of work, devices)
- Security
- Performance

### In-depth Reading
    - [KVM paper](https://www.kernel.org/doc/ols/2007/ols2007v1-pages-225-230.pdf)
    - [Firecracker paper](https://www.usenix.org/system/files/nsdi20-paper-wang-yonggang.pdf) (nsdi20)
    - [MacOS Hypervisor](https://developer.apple.com/documentation/hypervisor)
    - [Xen] (https://dl.acm.org/doi/10.1145/945445.945462)
    - Others: Disco, Qemu, Wine

### HYPERVISOR PARADIGM
| Category         | Linux KVM                      | macOS Hypervisor            |
| ---------------- | ------------------------------ | --------------------------- |
| Architecture     | OS as Hyperversior, Monolithic | User Space API              |
| Host OS          | Linux only                     | macOS only                  |
| Code Size        | Large; part of Linux kernel    | Large; part of macOS kernel |
| Hardware Drivers | Unified; uses Linux drivers    | Handled by macOS kernel     |
| HW Support       | VT-x / AMD-V                   | VT-x (Intel) / ARM (Apple)  |
| Isolation        | Process-level (strong)         | Sandbox-level (very strong) |

![KVM vs Hypervisor.framework Comparison](kvm-vs-hypervisor.jpg)




## Architecture Overview

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                              macOS (Host)                                    │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐ │
│  │                        User Space                                       │ │
│  │                                                                         │ │
│  │  ┌─────────────────────────┐      ┌─────────────────────────┐           │ │
│  │  │   TinyVMM Process #1    │      │   TinyVMM Process #2    │           │ │
│  │  │                         │      │                         │           │ │
│  │  │ ┌───────┐ ┌───────────┐ │      │ ┌───────┐ ┌───────────┐ │           │ │
│  │  │ │VM Loop│ │ Guest Mem │ │      │ │VM Loop│ │ Guest Mem │ │           │ │
│  │  │ │       │ │  (1MB)    │ │      │ │       │ │  (1MB)    │ │           │ │
│  │  │ │-Run   │ │ ┌───────┐ │ │      │ │-Run   │ │ ┌───────┐ │ │           │ │
│  │  │ │ vCPU  │ │ │Bare   │ │ │      │ │ vCPU  │ │ │Bare   │ │ │           │ │
│  │  │ │-Handle│ │ │Metal  │ │ │      │ │-Handle│ │ │Metal  │ │ │           │ │
│  │  │ │ exits │ │ │Code   │ │ │      │ │ exits │ │ │Code   │ │ │           │ │
│  │  │ └───────┘ │ └───────┘ │ │      │ └───────┘ │ └───────┘ │ │           │ │
│  │  │   vCPU    └───────────┘ │      │   vCPU    └───────────┘ │           │ │
│  │  └─────┬───────────────────┘      └─────┬───────────────────┘           │ │
│  │        │                                │                               │ │
│  │        │    Hypervisor.framework API    │                               │ │
│  │        │  ┌─────────────────────────┐   │                               │ │
│  │        └──►  hv_vm_create()         ◄───┘                               │ │
│  │           │  hv_vm_map()            │                                   │ │
│  │           │  hv_vcpu_create()       │                                   │ │
│  │           │  hv_vcpu_run()          │                                   │ │
│  │           │  hv_vcpu_get/set_reg()  │                                   │ │
│  │           └────────────┬────────────┘                                   │ │
│  └────────────────────────┼────────────────────────────────────────────────┘ │
│                           │ system calls                                     │
├───────────────────────────┼──────────────────────────────────────────────────┤
│                           ▼                                                  │
│  ┌─────────────────────────────────────────────────────────────────────────┐ │
│  │                     macOS Kernel (EL2 Hypervisor)                       │ │
│  │                                                                         │ │
│  │  Virtualized (per VM):              Not Virtualized by TinyVMM:         │ │
│  │  ┌─────────────────────┐            ┌─────────────────────┐             │ │
│  │  │ ✓ CPU (vCPU)        │            │ ✗ GPU               │             │ │
│  │  │ ✓ RAM (GPA mapping) │            │ ✗ Storage (no disk) │             │ │
│  │  │ ✓ System Registers  │            │ ✗ Network (no NIC)  │             │ │
│  │  │ ✓ Exception Levels  │            │ ✗ USB               │             │ │
│  │  │ ✓ Timers            │            │ ✗ Display           │             │ │
│  │  │ ✓ Interrupts (GIC)  │            │ ✗ Audio             │             │ │
│  │  └─────────────────────┘            └─────────────────────┘             │ │
│  │                                                                         │ │
│  │  Each VM gets isolated: vCPU state, page tables, trap handlers          │ │
│  └─────────────────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                       Apple Silicon Hardware (M1/M2/M3/M4)                   │
│                                                                              │
│  ┌──────────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐     │
│  │ ARM64 CPU Cores  │ │   Unified    │ │     GPU      │ │  Neural      │     │
│  │                  │ │   Memory     │ │              │ │  Engine      │     │
│  │ VHE (Virtualiz-  │ │              │ │ (not shared  │ │              │     │
│  │ ation Host Ext.) │ │ (shared by   │ │  with guest) │ │ (host only)  │     │
│  │                  │ │ host+guests) │ │              │ │              │     │
│  │ EL0: Guest User  │ └──────────────┘ └──────────────┘ └──────────────┘     │
│  │ EL1: Guest Kern  │                                                        │
│  │ EL2: Hypervisor◄─── TinyVMM traps here on HVC, MMIO, exceptions           │
│  └──────────────────┘                                                        │
└──────────────────────────────────────────────────────────────────────────────┘

What runs inside each VM (no guest OS!):
┌─────────────────────────────────────┐
│    Just bare-metal ARM64 code:      │
│    - Runs at EL1 (kernel mode)      │
│    - No OS, no drivers, no libc     │
│    - Communicates via HVC #0        │
│    - I/O only through hypercalls    │
│    - Isolated from other VMs        │
└─────────────────────────────────────┘
```

## Key Concepts

### Hypervisor vs VMM

| Term                              | What it is                                                                 | In this project                                         |
| --------------------------------- | -------------------------------------------------------------------------- | ------------------------------------------------------- |
| **Hypervisor**                    | System software that creates and manages VMs at the hardware level         | macOS kernel + Hypervisor.framework (provided by Apple) |
| **VMM (Virtual Machine Monitor)** | User-space program that controls a VM's lifecycle and handles its requests | TinyVMM (what we write)                                 |

The hypervisor provides the low-level capability; the VMM uses it to build a complete virtual environment.

### Exception Levels (ARM64 Privilege Rings)

ARM64 has 4 exception levels, similar to x86 ring levels:

```
┌─────────────────────────────────────────────────────────────┐
│ EL3: Secure Monitor    │ Secure firmware (not accessible)  │
├─────────────────────────────────────────────────────────────┤
│ EL2: Hypervisor        │ macOS hypervisor runs here        │
│                        │ Traps guest exceptions (HVC, etc) │
├─────────────────────────────────────────────────────────────┤
│ EL1: Kernel            │ Guest code runs here              │
│                        │ (our bare-metal code)             │
├─────────────────────────────────────────────────────────────┤
│ EL0: User              │ Guest user-space (unused by us)   │
└─────────────────────────────────────────────────────────────┘
        ▲ Lower privilege          Higher privilege ▲
```

TinyVMM's guest runs at EL1. When it executes `HVC #0`, the CPU traps to EL2 where macOS handles it and notifies our VMM.

### vCPU (Virtual CPU)

A vCPU is a software abstraction of a physical CPU core:

- **State**: Program counter (PC), stack pointer (SP), general registers (X0-X30), system registers, CPSR
- **Lifecycle**: Create → Configure → Run → Handle Exit → Run → ... → Destroy
- **Scheduling**: The host OS schedules vCPUs like regular threads

```c
hv_vcpu_create(&vcpu, &exit_info, NULL);  // Create vCPU
hv_vcpu_set_reg(vcpu, HV_REG_PC, 0x10000); // Set initial PC
hv_vcpu_run(vcpu);                         // Run until exit
```

### VM Exit

A VM exit occurs when guest execution must pause for the VMM to handle something:

| Exit Cause                 | Example                           | VMM Response                           |
| -------------------------- | --------------------------------- | -------------------------------------- |
| **Hypercall (HVC)**        | Guest wants to print a character  | Read registers, perform action, resume |
| **Memory fault**           | Guest accessed unmapped address   | Map memory or inject fault             |
| **System register access** | Guest read/wrote trapped register | Emulate the register                   |
| **Interrupt**              | Timer fired                       | Inject virtual interrupt               |
| **WFI/WFE**                | Guest is idle, waiting            | Yield CPU time                         |

The exit-handle-resume loop is the core of any VMM:
```
while (running) {
    hv_vcpu_run(vcpu);        // Guest runs...
    handle_exit(exit_info);   // ...until exit, then handle it
}
```

### Hypercall (HVC)

A hypercall is the guest's way to request services from the VMM (like a syscall, but guest→VMM instead of user→kernel):

```
Guest (EL1)                         VMM (User-space)
    │                                      │
    │  MOV X0, #1      ; hypercall number  │
    │  MOV X1, #'H'    ; argument          │
    │  HVC #0          ; trap to EL2 ──────┼──► exit_info->reason = EXCEPTION
    │                                      │    VMM reads X0, X1
    │                  ◄───────────────────┼─── VMM calls putchar('H')
    │  (continues)                         │    VMM advances PC, resumes
```

### Guest Physical Address (GPA) vs Host Virtual Address (HVA)

| Address Type             | Who sees it | Example                                |
| ------------------------ | ----------- | -------------------------------------- |
| **GPA** (Guest Physical) | Guest code  | `0x10000` (where guest thinks code is) |
| **HVA** (Host Virtual)   | VMM process | `0x102b24000` (actual malloc'd memory) |

The VMM maps HVA→GPA so the guest sees a contiguous address space starting at 0:

```c
void *host_mem = mmap(...);                              // HVA
hv_vm_map(host_mem, 0, size, HV_MEMORY_READ | ...);      // Map to GPA 0
// Guest sees: 0x0 - 0x100000
// Host sees:  host_mem pointer
```

### VHE (Virtualization Host Extensions)

VHE is an ARM64 feature that lets the host kernel run at EL2 (hypervisor level) instead of EL1. Benefits:

- **No mode switch overhead** when the kernel needs hypervisor features
- macOS uses VHE, so the kernel and hypervisor share EL2
- Guest runs at EL1, isolated by hardware

## Requirements

- macOS 11.0 (Big Sur) or later
- Apple Silicon (M1/M2/M3/M4)
- Xcode Command Line Tools (`xcode-select --install`)

## Project Structure

```
.
├── main.c              # The VMM implementation (~660 lines)
├── guest.S             # ARM64 assembly for guest code experiments
├── Makefile            # Build system with signing support
├── entitlements.plist  # macOS entitlement for hypervisor access
├── README.md           # This documentation
└── RELEASE_NOTES.md    # Version history and changes
```

| File                 | Description                                                                                                                                                                                 |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `main.c`             | The complete VMM implementation. Contains VM initialization, memory mapping, vCPU setup, the run loop, hypercall handlers, and embedded guest machine code. This is the main file to study. |
| `guest.S`            | Optional ARM64 assembly source for writing custom guest programs. You can modify this to experiment with different guest code, then assemble it with `make guest.bin`.                      |
| `Makefile`           | Build configuration that compiles the VMM, assembles guest code, and signs the binary with the required hypervisor entitlement.                                                             |
| `entitlements.plist` | Declares the `com.apple.security.hypervisor` entitlement required by macOS to allow a process to use Hypervisor.framework.                                                                  |

## Building & Running

```bash
make run
```

This builds the binary, signs it with the required `com.apple.security.hypervisor` entitlement, and runs it.

## Expected Output

```
╔════════════════════════════════════════╗
║   TinyVMM - macOS Hypervisor Demo      ║
║   Running 2 VMs in parallel            ║
╚════════════════════════════════════════╝

[VM 1] Creating virtual machine...
[VM 1] VM created successfully
[VM 1] Allocated 1024 KB guest memory at 0x...
[VM 1] Mapped guest memory: GPA 0x0 - 0x100000
[VM 1] Creating vCPU...
[VM 1] vCPU created
[VM 1] vCPU initialized: PC=0x10000, SP=0xff000
[VM 1] Loading guest code...
[VM 1] Loaded 352 bytes of guest code at GPA 0x10000
[VM 1] Starting guest execution...
[VM 1] --- Guest Output ---
Hello from VM 1!
VM 1: 0 1 2 3 4

[VM 1] Guest requested exit
[VM 1] --- End Guest Output ---
[VM 1] Cleaning up...
[VM 1] VM destroyed

[VM 1] Guest completed successfully!
[VM 2] Creating virtual machine...
[VM 2] VM created successfully
...
Hello from VM 2!
VM 2: 0 1 2 3 4
...
[VM 2] Guest completed successfully!
[Parent] Started VM 1 (PID ...) and VM 2 (PID ...)
[Parent] Waiting for VMs to complete...

[Parent] Both VMs finished.
[Parent] All VMs completed successfully!
```

## Code Walkthrough

### 1. VM Initialization (`vm_init`)

```c
// Create VM instance for this process
hv_vm_create(NULL);

// Allocate page-aligned memory for guest
void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

// Map host memory into guest physical address space
hv_vm_map(mem, 0, size, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
```

### 2. vCPU Setup (`vcpu_init`)

```c
// Create vCPU (returns exit info pointer)
hv_vcpu_create(&vcpu, &exit_info, NULL);

// Set initial register state
hv_vcpu_set_reg(vcpu, HV_REG_PC, code_address);   // Program counter
hv_vcpu_set_reg(vcpu, HV_REG_SP, stack_address);  // Stack pointer
hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0x3c5);        // EL1 mode
```

### 3. VM Run Loop (`vm_run`)

```c
while (running) {
    // Execute guest code until exit
    hv_vcpu_run(vcpu);

    // Handle the exit reason
    switch (exit_info->reason) {
        case HV_EXIT_REASON_EXCEPTION:
            // Check syndrome for HVC, faults, etc.
            handle_exception();
            break;
        // ... other cases
    }
}
```

### 4. Hypercall Handling

When guest executes `HVC #0`:

```c
// Read hypercall number and argument
hv_vcpu_get_reg(vcpu, HV_REG_X0, &hypercall_num);
hv_vcpu_get_reg(vcpu, HV_REG_X1, &argument);

switch (hypercall_num) {
    case 0: exit_vm(); break;
    case 1: putchar(argument); break;
}

// Advance PC past HVC instruction
hv_vcpu_set_reg(vcpu, HV_REG_PC, pc + 4);
```

## Hypercall Interface

| Number | Name    | x1 Argument     | Description       |
| ------ | ------- | --------------- | ----------------- |
| 0      | EXIT    | (unused)        | Terminate the VM  |
| 1      | PUTCHAR | ASCII character | Print a character |
| 2      | PUTS    | String address  | Print a string    |

## Experimenting

### Modify Guest Code

The guest code is embedded in `main.c` as ARM64 machine code. To experiment:

1. Write ARM64 assembly in `guest.S`
2. Assemble: `as -o guest.o guest.S`
3. Get hex: `objdump -d guest.o`
4. Update the `guest_code[]` array

Or load from file by modifying `load_guest()`.

### Add New Hypercalls

1. Define a new hypercall number
2. Add a case in `handle_hypercall()`
3. Use it from guest code with `HVC #0`

## Comparison to Firecracker

| Feature       | Firecracker                    | TinyVMM           |
| ------------- | ------------------------------ | ----------------- |
| Guest OS      | Full Linux kernel              | Bare metal code   |
| Memory        | GBs, dynamic                   | 1MB, static       |
| Devices       | virtio-net, virtio-blk, serial | None (hypercalls) |
| vCPUs         | Multiple                       | 1 per VM (2 VMs)  |
| Boot          | Linux boot protocol            | Direct jump       |
| Platform      | Linux KVM                      | macOS Hypervisor  |
| Lines of code | ~50,000                        | ~660              |

## Troubleshooting

### "Denied (missing entitlement?)"

The binary needs to be signed with the hypervisor entitlement. Running `make run` or `make` handles this automatically. If you're running the binary manually after modifying it:
```bash
codesign --entitlements entitlements.plist --force -s - tinyvmm
```

### "No device"

The Hypervisor.framework is not available. Ensure you're on:
- macOS 11.0+
- Apple Silicon Mac

### Crashes on hv_vcpu_run

Check that:
- PC points to valid, executable memory
- The guest code is correct ARM64
- Memory is properly mapped

## Further Reading

- [Apple Hypervisor Documentation](https://developer.apple.com/documentation/hypervisor)
- [ARM Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest)
- [Firecracker Design](https://github.com/firecracker-microvm/firecracker/blob/main/docs/design.md)

## License

MIT License - Feel free to use this for learning!
