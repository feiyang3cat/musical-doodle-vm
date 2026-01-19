/*
 * guest.S - Optional external guest code for TinyVMM
 *
 * This is an alternative to the embedded guest code in main.c.
 * You can modify this and rebuild to experiment with different guest programs.
 *
 * To use:
 *   make guest.bin
 *   # Then modify main.c to load from file instead of embedded array
 *
 * Hypercall interface:
 *   HVC #0 with:
 *     x0 = hypercall number
 *     x1 = argument
 *
 *   Hypercall 0: Exit VM
 *   Hypercall 1: Print character (x1 = ASCII char)
 *   Hypercall 2: Print string (x1 = guest address of null-terminated string)
 */

.global _start
.text

_start:
    /* Print "Hello from guest assembly!\n" */

    /* 'H' */
    mov     x1, #'H'
    mov     x0, #1          // HYPERCALL_PUTCHAR
    hvc     #0

    /* 'e' */
    mov     x1, #'e'
    mov     x0, #1
    hvc     #0

    /* 'l' */
    mov     x1, #'l'
    mov     x0, #1
    hvc     #0

    /* 'l' */
    mov     x1, #'l'
    mov     x0, #1
    hvc     #0

    /* 'o' */
    mov     x1, #'o'
    mov     x0, #1
    hvc     #0

    /* '!' */
    mov     x1, #'!'
    mov     x0, #1
    hvc     #0

    /* '\n' */
    mov     x1, #'\n'
    mov     x0, #1
    hvc     #0

    /* Exit */
    mov     x0, #0          // HYPERCALL_EXIT
    hvc     #0

    /* Infinite loop (should never reach) */
1:  b       1b
