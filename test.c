#include "mm.h"
#include "memlib.h"
int main () {

    mem_init();
    mm_init();

    size_t size = 1000;
    char *a = mm_malloc(size);
    printf("*a: %p\n", a);
    return 0;
}