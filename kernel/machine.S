	# outb(int port, int val)
	.global outb
outb:
	push %edx
	mov 8(%esp),%dx
	mov 12(%esp),%al
	outb %al,%dx
	pop %edx
	ret

	# outl(int port, int val)
        .global outl
outl:
        mov 4(%esp),%dx
        mov 8(%esp),%eax
        outl %eax,%dx
        ret

	# int inb(int port)
	.global inb
inb:
	push %edx
	mov 8(%esp),%dx
	inb %dx,%al
	pop %edx
	and $0xff,%eax
	ret

	# unsigned long inl(int port)
	.global inl
inl:
	push %edx
	mov 8(%esp),%dx
	inl %dx,%eax
	pop %edx
	ret

    .globl rdmsr
    # uint64_t rdmsr(uint32_t id)
rdmsr:
    mov 4(%esp), %ecx
    rdmsr
    ret

    .globl wrmsr
    # wrmsr (uint32_t id, uint64_t value)
wrmsr:
    mov 4(%esp), %ecx
    mov 8(%esp), %eax
    mov 12(%esp), %edx
    wrmsr
    ret

    .global spuriousHandler_
spuriousHandler_:
    iret

    .extern apitHandler
    .global apitHandler_
apitHandler_:
    // TODO: XMM, MMX, FP, ...
    pushf
    pusha
    push %esp
    call apitHandler
    pop %esp
    popa
    add $4, %esp
    iret

    .global sti
sti:
    sti
    ret

    .global cli
cli:
    cli
    ret

    .global getFlags
getFlags:
    pushf
    pop %eax
    ret

	/* memcpy(void* dest, void* src, size_t n) */
        .global memcpy
memcpy:
        mov 4(%esp),%eax       # dest
        mov 8(%esp),%edx       # src
        mov 12(%esp),%ecx      # n
        push %ebx
1:
        add $-1,%ecx
        jl 1f
        movb (%edx),%bl
        movb %bl,(%eax)
        add $1,%edx
        add $1,%eax
        jmp 1b
1:
        pop %ebx
        ret

     /* bzero(void* dest, size_t n) */
    .global bzero
bzero:
    mov 4(%esp),%eax       # dest
    mov 8(%esp),%ecx      # n
1:
    add $-1,%ecx
    jl 1f
    movb $0,(%eax)
    add $1,%eax
    jmp 1b
1:
    ret

	# ltr(uint32_t tr)
	.global ltr
ltr:
	mov 4(%esp),%eax
	ltr %ax
	ret

    # monitor(void* ptr)
    .global monitor
monitor:
    mov 4(%esp),%eax
    xor %ecx,%ecx
    xor %edx,%edx
    monitor
    ret

    # mwait()
    .global mwait
mwait:
    xor %eax,%eax
    xor %ecx,%ecx
    xor %edx,%edx
    mwait
    ret

    # cpuid(long eax, cpuid_out* out)
    #          12              16
    .global cpuid
cpuid:
    push %ebx          // moves everything by 4
    push %esi          // moves everything by 4+4=8
    mov 12(%esp),%eax
    cpuid
    mov 16(%esp),%esi
    mov %eax,0(%esi)
    mov %ebx,4(%esi)
    mov %ecx,8(%esi)
    mov %edx,12(%esi)
    pop %esi
    pop %ebx
    ret


	# switchToUser(pc,esp,eax)
        .global switchToUser
switchToUser:
        mov 4(%esp),%ecx        # pc
        mov 8(%esp),%ebx        # esp
        mov 12(%esp),%eax       # eax
        pushl userSS            # user SS
        pushl %ebx              # user ESP
        pushf                   # ring 3 flags
        orl $0x200,(%esp)       # enable interrupts
        pushl userCS            # user CS
        pushl %ecx              # user EIP
        iret

    #switchToUserWithParams(uint32_t pc, uint32_t esp, uint32_t arg1, uint32_t arg2);
        .global switchToUserWithParams
switchToUserWithParams:
        mov 4(%esp),%ecx        # pc
        mov 8(%esp),%ebx        # esp
        mov 12(%esp),%eax       # arg1
        mov %eax, -8(%ebx)
        mov 16(%esp), %eax      # arg2
        mov %eax, -4(%ebx)
        sub $12, %ebx

        pushl userSS            # user SS
        pushl %ebx              # user ESP
        pushf                   # ring 3 flags
        orl $0x200,(%esp)       # enable interrupts
        pushl userCS            # user CS
        pushl %ecx              # user EIP
        iret

	.global sysHandler_
sysHandler_:
        pushf
        pusha
        push %esp
        push %eax
        .extern sysHandler
        call sysHandler
        add $44,%esp
        iret

/* invlpg(uint32_t va) */
    .global invlpg
invlpg:
    mov 4(%esp),%eax
    invlpg (%eax)
    ret

    .global pageFaultHandler_
pageFaultHandler_:
    pushf
    pusha

    mov %esp,%eax
    push %eax       /* second argument */
    mov %cr2,%eax   /* address */
    push %eax       /* first argument (va) */

    .extern vmm_pageFault
    call vmm_pageFault   /* (va, sp) */

    add $8,%esp    /* pop arguments */

    popa
    popf

    add $4,%esp   /* pop error */
    iret

    /* vmm_on(uint32_t pd) */
    .global vmm_on
vmm_on:
    mov 4(%esp),%eax
    mov %eax,%cr3

    mov %cr0,%eax
    or $0x80000000,%eax
    mov %eax,%cr0
    ret

    /* uint32_t getCR3() */
    .global getCR3
getCR3:
    mov %cr3,%eax
    ret

    /* return_to_process_(uint32_t eax, uint32_t* loc); */
    .global return_to_process_
return_to_process_:

    movl 8(%esp), %edi

    movl 4(%edi), %eax
    movl 8(%edi), %ecx
    movl 12(%edi), %edx
    movl 16(%edi), %ebx
    movl 24(%edi), %ebp
    movl 28(%edi), %esi

    add $4, %esp
    pop %eax

    pushl userSS
    pushl 20(%edi)
    push 36(%edi)
    orl $0x200, (%esp)
    pushl userCS
    pushl (%edi)
    movl 32(%edi), %edi
    iret

    /* void return_to_process_(uint32_t* loc, uint32_t eax)
    .global return_to_process_with_val_
return_to_process_with_val_:
    
    movl 8(%esp), %eax

    movl 4(%esp), %edi
    push %eax
    
    push 20(%edi)
    push (%edi)

    movl 8(%edi), %ecx
    movl 12(%edi), %edx
    movl 16(%edi), %ebx
    movl 24(%edi), %ebp
    movl 28(%edi), %esi
    movl 32(%edi), %edi

    call switchToUser
    ret */