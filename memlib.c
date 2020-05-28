#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "memlib.h"

#define MAX_HEAP (20 * (1 << 20)) // 20M
// 内存系统模型
// 私有全局变量
static char *mem_heap;      // 堆开始的地址
static char *mem_brk;       // 指向用户空间使用了的空间地址加1
static char *mem_max_addr;  // 最大逻辑对的地址加1

/**
 * mem_init: 初始化内存系统模型
 */
void mem_init(void) 
{
    mem_heap = (char *)malloc(MAX_HEAP);
    mem_brk = (char *)mem_heap;
    mem_max_addr = (char *)(mem_heap + MAX_HEAP);
}

/**
 * mem_sbrk - 简单的sbrk方法
 *  扩展堆加incr 字节并且返回新区域的开始地址
 *  这个模块堆不能收缩
 *  mem_sbrk += incr
 */
void *mem_sbrk(int incr)
{
    char *old_brk = mem_brk;
    if ((incr < 0) || ((mem_brk + incr) > mem_max_addr)) {
        errno = ENOMEM;
        fprintf(stderr, "ERROR: mem_sbrk failed. Ran out of memory...\n");
        return (void *)-1;
    }
    mem_brk += incr;
    return (void *)old_brk;
}