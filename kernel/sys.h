#ifndef _SYS_H_
#define _SYS_H_

class SYS {
  public:
    static void init(void);
    static void do_exit(int rc);
};

void sigreturn();
#endif
