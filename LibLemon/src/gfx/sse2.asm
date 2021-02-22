global memcpy_sse2:function
global memset32_sse2:function
global memset64_sse2:function
global memcpy_sse2_unaligned:function
global memcpy_optimized:function

section .text

; (void* dest (rdi), void* src (rsi), uint64_t count (rdx))
memcpy_optimized:
	mov rcx, rdx
	shr rcx, 2 ; Get number of 16 byte chunks

	jz .pad2 ; skip to the end

	mov rcx, rdi
	and rcx, 0xf ; check if destination is aligned
	jz .smlcpy

	shr rcx, 2 ; Addresses should be 32-bit aligned

	neg rcx ; negate rcx
	add rcx, 4 ; add 4 to get (4 - (count & 0x3))

	sub rdx, rcx

.pad1: ; pad out the destination until 16 byte aligned, as per the ABI the DF should be 0 meaning that rdi and rsi will be incremented
	lodsd
	stosd
	loop .pad1

.smlcpy:
	mov rax, rsi
	and rax, 0xf
	jnz .smlcpyua

	mov rcx, rdx
	and rcx, ~0x3
	jz .pad2
	shl rcx, 2

	lea rsi, [rsi + rcx]
	lea rdi, [rdi + rcx]

	neg rcx

.smlcpyloop:
	prefetchnta [rsi + rcx + 16]

	movdqu xmm0, [rsi + rcx]

	movntdq [rdi + rcx], xmm0

	add rcx, 16
	jnz .smlcpyloop

	and rdx, 0x3

.pad2: ; pad out the end
	mov rcx, rdx
	jz ._ret

.pad2l:
	lodsd
	stosd
	loop .pad2l

._ret:
	sfence

	ret

.smlcpyua:
	prefetchnta [rsi + rcx + 16]

	mov rcx, rdx
	and rcx, ~0x3
	jz .pad2u
	shl rcx, 2

	lea rsi, [rsi + rcx]
	lea rdi, [rdi + rcx]

	neg rcx

.smlcpyloopua:
	movdqu xmm0, [rsi + rcx]

	movntdq [rdi + rcx], xmm0

	add rcx, 16
	jnz .smlcpyloopua

	and rdx, 0x3

.pad2u: ; pad out the end
	mov rcx, rdx
	jz .retua

.pad2lua:
	lodsd
	stosd
	loop .pad2lua

.retua:
	sfence
	
	ret

memcpy_sse2:
	xor rax, rax
	mov rcx, rdx
	
	test rcx, rcx
	jz .ret
.loop:
	movdqa xmm0, [rsi + rax]

	movdqa [rdi + rax], xmm0

	add rax, 0x10
	loop .loop
.ret:
	ret

memcpy_sse2_unaligned:
	xor rax, rax
	mov rcx, rdx
	
	test rcx, rcx
	jz .ret
.loop:
	movdqu xmm0, [rsi + rax]

	movdqu [rdi + rax], xmm0

	add rax, 0x10
	loop .loop
	
.ret:
	ret

memset32_sse2:
	push rbp    ; save the prior rbp value
    mov rbp, rsp

	mov rcx, rdx

	cmp rcx, 1
	jle .ret ; Avoid 0s
	
	movq xmm1, rsi
	pxor xmm0, xmm0

	paddq xmm0, xmm1
	pslldq xmm0, 4
	paddq xmm0, xmm1
	pslldq xmm0, 4
	paddq xmm0, xmm1
	pslldq xmm0, 4
	paddq xmm0, xmm1

.loop:
	movdqa [rdi], xmm0
	add rdi, 0x10
	
	loop .loop

.ret:
	mov rsp, rbp
	pop rbp

	ret

memset64_sse2:
	push rbp    ; save the prior rbp value
    mov rbp, rsp

	mov rax, rcx
	mov rbx, rdx
	mov rcx, r8

	cmp rcx, 1
	jle .ret ; Avoid 0s
	
	movq xmm1, rbx
	pxor xmm0, xmm0

	paddq xmm0, xmm1
	pslldq xmm0, 8
	paddq xmm0, xmm1

.loop:
	movdqa [rax], xmm0
	add rax, 0x10
	
	loop .loop

.ret:
	mov rsp, rbp
	pop rbp

	ret