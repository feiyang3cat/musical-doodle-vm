# DOODLE-VMM

Learning and Hello World Project for Hypervisor/VMM/Sandbox Development.

## Chpt-1: Knowledge

**hodgepodge of terms**:
Hypervisor, VMM, VM, GuestOS, BareMetal, MicroKernel, UniKernel, Monolithic Kernel, Hybrid Kernel
Hypercalls, Systemcalls, Traps/Interrupts/Exceptions, Privilege Levels(mode/ring/exception level)

**stacks**:
```
Traditional Stack:                   Virtualization Stack:
┌─────────────┐                      ┌─────────────┐
│ Application │                      │ Application │
└─────┬───────┘                      └─────┬───────┘
      │ (syscall)                           │ (syscall)
┌─────▼───────┐                      ┌─────▼─────────────┐
│ OS/Kernel   │                      │ Guest OS/Kernel   │
└─────┬───────┘                      └─────┬─────────────┘
      │ (direct HW access)                 │ (hypercall)
┌─────▼───────┐                      ┌─────▼─────────────┐
│ Hardware    │                      │ Hypervisor + VMM  │
│ (CPU/Mem)   │                      └─────┬─────────────┘
└─────────────┘                            │ (HW instructions)
                                   ┌───────▼────────────┐
                                   │ CPU / Memory /     │
                                   │ Devices (Hardware) │
                                   └────────────────────┘
```
**basic problems to solve**:

The management of multiple OSes in the same machine:
- thread related
- memory management
- IO device management (a lot of work, devices)
- Security
- Performance

**players in the game**
firecracker, kvm, qemu, wine, xen, macos-hypervisor-framework
- firecracker: lightweight VM manager, and IO virtualization/emulation -> on top of kvm
- qemu: emulator and virtualizer -> on top of linux kernel, interchanged by firecracker
- kvm: a virtualization infrastructure for Linux -> on top of linux kernel
- wine: a runtime compatibility layer for running Windows applications on Linux -> on top of linux kernel
- macos-hypervisor-framework: a virtualization infrastructure for macOS -> on top of macos kernel


**HYPERVISOR PARADIGM**:
| Category         | Linux KVM                      | macOS Hypervisor            |
| ---------------- | ------------------------------ | --------------------------- |
| Architecture     | OS as Hyperversior, Monolithic | User Space API              |
| Host OS          | Linux only                     | macOS only                  |
| Code Size        | Large; part of Linux kernel    | Large; part of macOS kernel |
| Hardware Drivers | Unified; uses Linux drivers    | Handled by macOS kernel     |
| HW Support       | VT-x / AMD-V                   | VT-x (Intel) / ARM (Apple)  |
| Isolation        | Process-level (strong)         | Sandbox-level (very strong) |

<img src="./assets/kvm-vs-hypervisor.jpg" alt="KVM vs Hypervisor.framework Comparison" width="600"/>


## Chpt-2: Project

### 2.1) Learning the FireCracker Solution

Reading:
    - [KVM paper](https://www.kernel.org/doc/ols/2007/ols2007v1-pages-225-230.pdf)
    - [Firecracker paper](https://www.usenix.org/system/files/nsdi20-paper-wang-yonggang.pdf) (nsdi20)

### 2.2) MacOS Hello World - TinyVMM

See the [tinyvmm/](./tinyvmm/) folder for a minimal VMM implementation using macOS Hypervisor.framework.
