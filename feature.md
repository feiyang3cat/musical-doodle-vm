
  Current State

  The guest application is bare-metal ARM64 machine code embedded directly in main.c (lines 86-428). It
  runs at CPU Exception Level 1 with:
  - No operating system
  - No runtime libraries
  - Only hypercalls (HVC instructions) to communicate with the VMM host

  Challenge with "Raw Python Code"

  Python is an interpreted language that requires a runtime. The current VM architecture has no OS or
  interpreter - just raw CPU instructions. There are several approaches:

  Option 1: Embed MicroPython (Recommended for Simplicity)

  Work involved:
  - Port MicroPython to run bare-metal on ARM64
  - Compile it as the guest payload
  - Add hypercall bindings for I/O
  - ~Medium complexity, but gives real Python

  Option 2: Boot a Full Linux Kernel

  Work involved:
  - Replace guest code with a Linux kernel + initramfs
  - Add virtio devices for console/disk
  - Run Python in userspace
  - ~Significant VMM changes (interrupts, device emulation)

  Option 3: Python-to-ARM64 Transpiler

  Work involved:
  - Build/use a tool that compiles Python to native ARM64
  - Would lose dynamic features (eval, imports, etc.)
  - ~High complexity

  Option 4: Host-side Python (Hybrid)

  Work involved:
  - Keep guest as machine code
  - Write Python on the host that generates guest code or controls VM behavior
  - Minimal VMM changes

  Option 5: Custom Bytecode Interpreter in Guest

  Work involved:
  - Write an ARM64 assembly interpreter for Python bytecode
  - Very limited Python subset
  - ~High complexity

  ---
  Which approach interests you? The answer affects the scope significantly - from a few days of work
  (Option 4) to potentially weeks (Options 1-3).
