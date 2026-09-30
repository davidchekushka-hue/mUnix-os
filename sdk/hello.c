#include "syscall.h"
int main(void) {
    sys_write("Hello from a third-party mUnix app!\n");
    sys_fill_rect(200, 200, 400, 300, 0xFF00CC44);
    return 0;
}
