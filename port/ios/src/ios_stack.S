/*
IOS_STACK.S

void ios_run_on_stack(void (*function)(void *), void *argument, void *stack_top)

Calls function(argument) with the stack pointer moved to stack_top, and
returns on the caller's stack once it returns. iOS's main thread has a 1 MB
stack and SDL's video and events must be called on that thread
(ios_main.c); the game was written for more (the desktops' main threads
have 8 MB, the Android port's game thread 16 MB). The caller's stack
pointer is kept on the new stack, and the frame record links the new frames
to the caller's, so stack walks (the crash report's) go through.
*/

	.text
	.globl	_ios_run_on_stack
	.p2align	2
_ios_run_on_stack:
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
	/* the caller's stack pointer, kept on the new stack (16-byte aligned) */
	mov	x9, sp
	and	x2, x2, #0xfffffffffffffff0
	mov	sp, x2
	stp	x9, xzr, [sp, #-16]!
	mov	x16, x0
	mov	x0, x1
	blr	x16
	ldp	x9, x10, [sp], #16
	mov	sp, x9
	ldp	x29, x30, [sp], #16
	ret
