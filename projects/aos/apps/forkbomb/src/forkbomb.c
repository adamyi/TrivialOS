#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sel4/sel4.h>
#include <syscalls.h>
#include <sys/mman.h>

#include <sos.h>

#include <utils/page.h>

// create two forks then quit ourself

int mypid;

void myprintf(const char *fmt, ...) {
    char buffer[100];
    va_list vl;
    va_start(vl, fmt);
    vsnprintf(buffer, 100, fmt, vl);
    va_end(vl);
    puts(buffer);
}

void myfork() {
    int pid;
    while ((pid = sos_process_create("forkbomb")) < 0) sleep(1);
    myprintf("fork bomb: %d created %d", mypid, pid);
}

int main(void) {
    sosapi_init_syscall_table();

    mypid = sos_my_id();

    myprintf("fork bomb : %d hello", mypid);

    myfork();
    myfork();

    myprintf("fork bomb: %d sleeps", mypid);
    sleep(3);

    myprintf("fork bomb: %d quits", mypid);
    exit(0);

    return 0;
}
