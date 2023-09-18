	#
	# user-side system calls
	#
	# System calls use a special convention:
        #     %eax  -  system call number
        #
        #

	# void exit(int status)
	.global exit
exit:
	mov $0,%eax
	int $48
	ret

	# ssize_t write(int fd, void* buf, size_t nbyte)
	.global write
write:
	mov $1,%eax
	int $48
	ret

        # int fork()
        .global fork
fork:
        push %ebx
        push %esi
        push %edi
        push %ebp
        mov $2,%eax
        int $48
        pop %ebp
        pop %edi
        pop %esi
        pop %ebx
        ret

	# int shutdown(void)
        .global shutdown
shutdown:
        mov $7,%eax
        int $48
        ret

	# int execl(const char *pathname, const char *arg, ...
        #               /* (char  *) NULL */);
        .global execl
execl:
	mov $1000,%eax
	int $48
	ret


        # unsigned sem()
        .global sem
sem:
	mov $1001,%eax
	int $48
	ret

        # void up(unsigned)
        .global up
up:
	mov $1002,%eax
	int $48
	ret

        # void down(unsigned)
        .global down
down:
	mov $1003,%eax
	int $48
	ret

	# void simple_signal(handler)
	.global simple_signal
simple_signal:
	mov $1004,%eax
	int $48
	ret

        # int join()
        .global join
join:
        mov $999,%eax
        int $48
        ret
