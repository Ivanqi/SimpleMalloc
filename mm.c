#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

// 基础常量

// 单字(4) 或 双字(8)对齐
#define ALIGNMENT 8

#define WSIZE 4                     // 字或者说头或者尾的大小（单位字节）
#define DSIZE 8                     // 双字
#define CHUNKSIZE (1 << 12)         // 扩展堆4096个字节

#define MAX(x, y) ((x) > (y) ? (x) : (y))

// 将大小和分配的位打包成一个字
#define PACK(size, alloc) ((size) | (alloc))

// 在地址p做读写操作
#define GET(p)  (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

// 从地址p读取大小和分配字段. 从地址p处的头部或者脚部分别返回大小和已分配位
#define GET_SIZE(p) (GET(p) & ~0x7)      /*size of block*/
#define GET_ALLOC(p)    (GET(p) & 0x1)    /*block is alloc?*/

// 给定块ptr bp, 计算其页眉和页脚地址
#define HDRP(bp)    ((char *)(bp) - WSIZE)                        /*head of block*/
#define FTRP(bp)    ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)     /*foot of block*/

// 给定块ptr bp, 计算下一个和上一块的地址
#define NEXT_BLKP(bp)   ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))   /*next block*/
#define PREV_BLKP(bp)   ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))   /*prev block*/

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static char *heap_listp = 0;

// mem_init函数将对于堆来说可用的虚拟内存模型化为一个大的，双字对齐的字节数组
int mm_init(void)
{
    /**
     * 创建空堆，创建一个空闲链表
     */
    heap_listp = mem_sbrk(4 * WSIZE);
    if (heap_listp == (void *) -1) {    // 16字节
        return -1;
    }

    PUT(heap_listp, 0);                                 // *(unsigned int *)heap_listp = 0
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));      // *(heap_listp + 4) = PACK(8, 1)
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));      // *(heap_listp + 8) = PACK(8, 1)
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));          // *(heap_listp + 12) = PACK(0, 1)
    heap_listp += (2 * WSIZE);                          // heap_listp += 8，heap_listp 是 char *

    // 扩展4096字节空间
    if ((extend_heap(CHUNKSIZE / WSIZE)) == NULL) {
        return -1;
    }

    return 0;
}

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    // 分配偶数个单词以保持对齐
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    bp = mem_sbrk(size);
    if ((long) bp == -1) {
        return NULL;
    }

    // 初始化自由块 头部/脚部 和结尾块
    /**
     * PUT(HDRP(bp), PACK(size, 0)); 往bp-4的地址上写入4096的值
     * PUT(FTRP(bp), PACK(size, 0));
     *  FTRP(bp) 获取 bp-4地址上的4906的值。bp地址加上4096字节，然后通过-8，相当于空出了尾部和结尾块
     *  然后往尾部写入4096这个值
     * PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
     *  NEXT_BLKP(bp)，往bp-4的地址上获取4096的值，然后bp+4096获取整个分配块的大小
     *  HDRP(NEXT_BLKP(bp)), 然后 (bp + 4096) - 4，得到结尾块
     *  最后往结尾块写入 1
     */
    PUT(HDRP(bp), PACK(size, 0));   // 4096字节空间大小，包括头和尾
    PUT(FTRP(bp), PACK(size, 0));   // ...
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));   // 结尾块

    // 如果前一个块空闲，则合并
    return coalesce(bp);
}

void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0));   // 变成空闲块
    PUT(FTRP(bp), PACK(size, 0));
}

/**
 * FTRP(PREV_BLKP(bp))
 *  PREV_BLKP(bp) =  (char*)(bp) - ((*(unsigned int *)(((char *)(bp)) - 8)) & ~0x7)
 *      1. 通过使用当前块(bp)的首地址 - 8, 获取上一个块的尾部
 *      2. 然后通过上一个块的尾部获取整个块的大小
 *      3. 通过bp - 上一个块的尾部获取整个块的大小 得到 尾部地址
 *  HDRP(bp) = ((PREV_BLKP(bp) - 4)
 *      1. 尾部地址-4，获取首部地址
 *  GETSIZE(HDRP(bp)) = ((*(unsigned int *)(HDRP(bp)) & ~0x7)
 *      1. 通过首部地址，获取整个块的小
 *  最后： ((char *)(bp) + GETSIZE(HDRP(bp)) - 8) = PREV_BLKP(bp) + GETSIZE(HDRP(bp)) - 8)
 *      1. 尾部地址 + 块大小 - 8
 *      2. 等到上一个块的尾部地址
 *  
 * HDRP(NEXT_BLKP(bp))
 *  NEXT_BLKP(bp) = (char*)(bp) + ((*(unsigned int *)(((char *)(bp)) - 4)) & ~0x7)
 *      1. 首先获取当前块的大小
 *      2. 然后当前块的首地址 + 块大小 = 下一个块的地址
 *  最后: NEXT_BLKP(bp) - 4
 *      1. 下一个块的地址-4，获取下一个块的首部信息
 */

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    // 情况1: 前面的块和后面的块都是已分配的
    if (prev_alloc && next_alloc) {
        /**
         * 两个邻接的块都是已经分配的，因此不可能进行合并
         * 所以当前块的状态只是简单地从分配变成空闲
         */
        return bp;
    } else if (prev_alloc && !next_alloc) {     // 情况2: 前面的块是分分配的，后面的块的空闲的
        /**
         * 当前块和与后面的块合并
         * 用当前块和后面的大小的和来更新当前块头部和后面的脚部
         */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {     // 情况3：前面的块是空闲的，后面的块是已分配的
        /**
         * 前面的块和当前块合并
         * 用两个块大小的和更新前面块的头部和当前的脚部
         */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else {                                    // 情况4：前面的和后面的块都是空闲的
        /**
         * 要合并三个块形成的一个单独的空闲块
         * 用三个块大小的和来更新前面的头部和后面块的脚部
         */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
}

void *mm_malloc(size_t size)
{
    size_t asize;               // 调整块的大小， 要为头和尾留有空间
    size_t extendsize;          // 堆中如果没有合适的块，需要扩展的扩展的块的大小
    char *bp;

    if (size == 0) return NULL;

    if (size <= DSIZE) {
        asize = 2 * DSIZE;      // 最小块大小16字节(头 + 尾 = 8字节， 另外8字节对齐要求，包括了size的大小)
    } else {
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);   // 向上舍入最接近的8的整倍数
    }

    if ((bp = find_fit(asize)) != NULL) {   // 搜索空闲块，找到合适的空闲块
        place(bp, asize);                   // 分割出多余部分
        return bp;                          // 返回新分配的地址
    }

    // 如果没有找到，需要扩展，最小扩展4096
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) {
        return NULL;
    }

    place(bp, asize);
    return bp;
}

static void *find_fit(size_t asize)
{
    // 首次适配(first fit)
    void *bp;
    for (bp = heap_listp; GET_SIZE(HDRP(bp)); bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            return bp;
        }
    }
    return NULL;
}

static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    // 最小块是16字节
    if ((csize - asize) >= (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    } else {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
    }
}