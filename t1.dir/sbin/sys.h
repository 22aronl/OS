#ifndef _SYS_H_
#define _SYS_H_

/****************/
/* System calls */
/****************/

typedef int ssize_t;
typedef unsigned int size_t;

/* exit */
extern void exit(int rc);

/* write */
extern ssize_t write(int fd, void* buf, size_t nbyte);

/* fork */
extern int fork();

/* execl */
extern int execl(const char *pathname, const char *arg, ...
                       /* (char  *) NULL */);

/* shutdown */
extern void shutdown(void);

/* join */
extern int join(void);

/* sem */
extern int sem(unsigned int);

/* up */
extern int up(unsigned int);

/* down */
extern int down(unsigned int);

extern void simple_signal(void (*pf)(int, unsigned int));

#endif
