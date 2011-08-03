
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_PALLOC_H_INCLUDED_
#define _NGX_PALLOC_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * NGX_MAX_ALLOC_FROM_POOL should be (ngx_pagesize - 1), i.e. 4095 on x86.
 * On Windows NT it decreases a number of locked pages in a kernel.
 */
#define NGX_MAX_ALLOC_FROM_POOL  (ngx_pagesize - 1)

#define NGX_DEFAULT_POOL_SIZE    (16 * 1024)

#define NGX_POOL_ALIGNMENT       16
#define NGX_MIN_POOL_SIZE                                                     \
    ngx_align((sizeof(ngx_pool_t) + 2 * sizeof(ngx_pool_large_t)),            \
              NGX_POOL_ALIGNMENT)


/* 资源释放函数指针 */
typedef void (*ngx_pool_cleanup_pt)(void *data);

typedef struct ngx_pool_cleanup_s  ngx_pool_cleanup_t;

struct ngx_pool_cleanup_s 
{
    ngx_pool_cleanup_pt   handler;      /* 资源释放函数 */
    void                 *data;         /* 传递给资源释放函数的参数 */
    ngx_pool_cleanup_t   *next;
};


typedef struct ngx_pool_large_s  ngx_pool_large_t;

struct ngx_pool_large_s {
    ngx_pool_large_t     *next;
    void                 *alloc;       /* 指向大块内存 */
};


typedef struct 
{
    u_char               *last;         // 当前内存分配的结束位置，即下一段可分配内存的起始位置
    u_char               *end;          // 内存池的结束位置
    ngx_pool_t           *next;         // 链接到下一个内存池
    ngx_uint_t            failed;       // 统计当前内存池不能满足内存请求的次数
} ngx_pool_data_t;


struct ngx_pool_s 
{
    ngx_pool_data_t       d;
    size_t                max;          // 由内存池分配的小块内存的最大值
    ngx_pool_t           *current;      // 当前内存池
    ngx_chain_t          *chain;        
    ngx_pool_large_t     *large;        // 分配大块内存用，即超过max的内存请求
    ngx_pool_cleanup_t   *cleanup;      // 挂载一些内存池释放的时候，需要同时释放的一些资源
    ngx_log_t            *log;
};


typedef struct {
    ngx_fd_t              fd;
    u_char               *name;
    ngx_log_t            *log;
} ngx_pool_cleanup_file_t;


void *ngx_alloc(size_t size, ngx_log_t *log);
void *ngx_calloc(size_t size, ngx_log_t *log);

ngx_pool_t *ngx_create_pool(size_t size, ngx_log_t *log);
void ngx_destroy_pool(ngx_pool_t *pool);
void ngx_reset_pool(ngx_pool_t *pool);

void *ngx_palloc(ngx_pool_t *pool, size_t size);
void *ngx_pnalloc(ngx_pool_t *pool, size_t size);
void *ngx_pcalloc(ngx_pool_t *pool, size_t size);
void *ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment);
ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p);


ngx_pool_cleanup_t *ngx_pool_cleanup_add(ngx_pool_t *p, size_t size);
void ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd);
void ngx_pool_cleanup_file(void *data);
void ngx_pool_delete_file(void *data);


#endif /* _NGX_PALLOC_H_INCLUDED_ */
