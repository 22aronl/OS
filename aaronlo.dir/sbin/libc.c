#include "libc.h"

int putchar(int c) {
    char t = (char)c;
    return write(1,&t,1);
}

int puts(const char* p) {
    char c;
    int count = 0;
    while ((c = *p++) != 0) {
        int n = putchar(c); 
        if (n < 0) return n;
        count ++;
    }
    putchar('\n');
    
    return count+1;
}
