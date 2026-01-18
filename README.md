# TinyVMM - A Minimal Hypervisor for macOS

A "Hello World" Virtual Machine Monitor (VMM) demonstrating how to create a tiny sandbox on macOS using Apple's Hypervisor.framework. This is an educational project inspired by [Firecracker](https://github.com/firecracker-microvm/firecracker), simplified to its bare essentials.

## What is this?

TinyVMM is the simplest possible hypervisor that actually runs guest code. It demonstrates:

- **VM Creation**: How to initialize a virtual machine instance
- **Memory Mapping**: Setting up guest physical address space
- **vCPU Management**: Creating and configuring a virtual CPU
- **Guest Execution**: Running ARM64 code in the VM
- **VM Exits**: Handling when the guest needs VMM assistance
- **Hypercalls**: A simple guest-to-host communication mechanism

## Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│                    Your macOS Process                    │
│  ┌────────────────────────────────────────────────────┐  │
│  │                      TinyVMM                       │  │
│  │                                                    │  │
│  │   ┌─────────────┐     ┌─────────────────────────┐  │  │
│  │   │   VM Loop   │◄───►│  Hypervisor.framework   │  │  │
│  │   │             │     │     (Apple's API)       │  │  │
│  │   │ - Run vCPU  │     └───────────┬─────────────┘  │  │
│  │   │ - Handle    │                 │                │  │
│  │   │   Exits     │                 ▼                │  │
│  │   │ - Hypercalls│     ┌─────────────────────────┐  │  │
│  │   └─────────────┘     │    macOS Hypervisor     │  │  │
│  │                       │        (Kernel)         │  │  │
│  └───────────────────────┴─────────────────────────┴──┘  │
└──────────────────────────────────────────────────────────┘
                              │
                              ▼
                ┌─────────────────────────────┐
                │   Apple Silicon Hardware    │
                │   (ARM64 Virtualization)    │
                └─────────────────────────────┘
```

## Requirements

- macOS 11.0 (Big Sur) or later
- Apple Silicon (M1/M2/M3/M4)
- Xcode Command Line Tools (`xcode-select --install`)

## Project Structure

```
.
├── main.c              # The VMM implementation (~400 lines)
├── guest.S             # ARM64 assembly for guest code experiments
├── Makefile            # Build system with signing support
├── entitlements.plist  # macOS entitlement for hypervisor access
├── README.md           # This documentation
└── RELEASE_NOTES.md    # Version history and changes
```

| File | Description |
|------|-------------|
| `main.c` | The complete VMM implementation. Contains VM initialization, memory mapping, vCPU setup, the run loop, hypercall handlers, and embedded guest machine code. This is the main file to study. |
| `guest.S` | Optional ARM64 assembly source for writing custom guest programs. You can modify this to experiment with different guest code, then assemble it with `make guest.bin`. |
| `Makefile` | Build configuration that compiles the VMM, assembles guest code, and signs the binary with the required hypervisor entitlement. |
| `entitlements.plist` | Declares the `com.apple.security.hypervisor` entitlement required by macOS to allow a process to use Hypervisor.framework. |

## Building & Running

```bash
make run
```

This builds the binary, signs it with the required `com.apple.security.hypervisor` entitlement, and runs it.

## Expected Output

```
╔════════════════════════════════════════╗
║   TinyVMM - macOS Hypervisor Demo      ║
║   A minimal VMM for learning           ║
╚════════════════════════════════════════╝

[VMM] Creating virtual machine...
[VMM] VM created successfully
[VMM] Allocated 1024 KB guest memory at 0x...
[VMM] Mapped guest memory: GPA 0x0 - 0x100000
[VMM] Creating vCPU...
[VMM] vCPU created with ID: 0
[VMM] vCPU initialized: PC=0x10000, SP=0xff000
[VMM] Loading guest code...
[VMM] Loaded 248 bytes of guest code at GPA 0x10000
[VMM] Starting guest execution...
[VMM] --- Guest Output ---
Hello from VM!
0 1 2 3 4

[VMM] Guest requested exit
[VMM] --- End Guest Output ---
[VMM] Cleaning up...
[VMM] VM destroyed

[VMM] Guest completed successfully!
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

## Key Concepts

### Guest Physical Address (GPA)

The address space the guest sees. We map host memory to GPA 0, so the guest thinks it has RAM starting at address 0.

### Exception Levels (EL)

ARM64 has 4 exception levels:
- **EL0**: User mode (applications)
- **EL1**: Kernel mode (where our guest runs)
- **EL2**: Hypervisor mode (handled by Apple's hypervisor)
- **EL3**: Secure monitor (not available)

### VM Exits

The guest can't run forever - it "exits" to the VMM when:
- It executes HVC (hypercall)
- It accesses memory we haven't mapped
- It accesses system registers we trap
- The VMM explicitly stops it

### ESR_EL2 (Exception Syndrome Register)

When an exception occurs, this register tells us why:
- **EC** (Exception Class): The type of exception (bits 31:26)
- Other fields give additional details

## Hypercall Interface

| Number | Name      | x1 Argument      | Description           |
|--------|-----------|------------------|-----------------------|
| 0      | EXIT      | (unused)         | Terminate the VM      |
| 1      | PUTCHAR   | ASCII character  | Print a character     |
| 2      | PUTS      | String address   | Print a string        |

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

| Feature           | Firecracker                    | TinyVMM              |
|-------------------|--------------------------------|----------------------|
| Guest OS          | Full Linux kernel              | Bare metal code      |
| Memory            | GBs, dynamic                   | 1MB, static          |
| Devices           | virtio-net, virtio-blk, serial | None (hypercalls)    |
| vCPUs             | Multiple                       | Single               |
| Boot              | Linux boot protocol            | Direct jump          |
| Platform          | Linux KVM                      | macOS Hypervisor     |
| Lines of code     | ~50,000                        | ~400                 |

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
