
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


static void *ngx_palloc_block(ngx_pool_t *pool, size_t size);
static void *ngx_palloc_large(ngx_pool_t *pool, size_t size);



/**
*   创建内存池
*/
ngx_pool_t* ngx_create_pool(size_t size, ngx_log_t *log)
{
    ngx_pool_t  *p;

    p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);    // 分配内存
    if (p == NULL)      // 分配内存失败
    {
        return NULL;
    }

    p->d.last = (u_char *)p + sizeof(ngx_pool_t);
    p->d.end = (u_char *)p + size;
    p->d.next = NULL;
    p->d.failed = 0;

    size = size - sizeof(ngx_pool_t);
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    p->current = p;
    p->chain = NULL;
    p->large = NULL;
    p->cleanup = NULL;
    p->log = log;

    return p;
}



/**
*   销毁内存池
*/
void ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t          *p, *n;
    ngx_pool_large_t    *l;
    ngx_pool_cleanup_t  *c;


    /* 调用资源释放函数 */
    for (c = pool->cleanup; c; c = c->next) 
    {
        if (c->handler) 
        {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "run cleanup: %p", c);
            c->handler(c->data);
        }
    }

    /* 释放大块内存 */
    for (l = pool->large; l; l = l->next) 
    {

        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "free: %p", l->alloc);

        if (l->alloc) 
        {
            ngx_free(l->alloc);
        }
    }

#if (NGX_DEBUG)

    /*
     * we could allocate the pool->log from this pool
     * so we can not use this log while the free()ing the pool
     */

    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                       "free: %p, unused: %uz", p, p->d.end - p->d.last);

        if (n == NULL) {
            break;
        }
    }

#endif

    /* 释放内存池链表中的各个内存池 */
    for (p = pool, n = pool->d.next; p = n, n = n->d.next) 
    {
        ngx_free(p);

        if (n == NULL) 
        {
            break;
        }
    }
}


/**
*   重置内存池
*/
void ngx_reset_pool(ngx_pool_t *pool)
{
    ngx_pool_t        *p;
    ngx_pool_large_t  *l;


    /* 释放大块内存 */
    for (l = pool->large; l; l = l->next) 
    {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }

    pool->large = NULL;

    /* 重置内存池链表中的各个内存池 */
    for (p = pool; p; p = p->d.next) 
    {
        p->d.last = (u_char *)p + sizeof(ngx_pool_t);
    }
}



/**
*   从内存池中分配内存
*/
void * ngx_palloc(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    ngx_pool_t  *p;

    if (size <= pool->max)      /* 判断能否由内存池分配 */
    {

        p = pool->current;      /* 获取当前可用的内存池 */

        do {
            m = ngx_align_ptr(p->d.last, NGX_ALIGNMENT);

            if ((size_t)(p->d.end - m) >= size)     /* 判断内存池的空余空间能否满足需求 */
            {
                p->d.last = m + size;

                return m;
            }

            p = p->d.next;      /* 尝试从下一个内存池中分配 */

        } while (p);

        /* 当前内存池链表中的所有内存池都无法满足需求，则创建新的内存池 */
        return ngx_palloc_block(pool, size);
    }

    /* 大块内存的分配 */
    return ngx_palloc_large(pool, size);
}



/**
*   函数名: ngx_pnalloc
*   功能:   分配内存
*   参数:   
            [in]pool: 内存池
            [in]size: 欲分配的内存块的大小
*   返回值:
*/
void* ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    ngx_pool_t  *p;

    if (size <= pool->max)      // 判断内存分配请求是否是从内存池中分配
    {

        p = pool->current;

        do 
        {
            m = p->d.last;

            if ((size_t)(p->d.end - m) >= size)     // 判断内存池中的空余空间能否满足此次分配大小
            {
                p->d.last = m + size;

                return m;
            }

            p = p->d.next;      // 交给下一个内存池进行分配

        } while(p);

        return ngx_palloc_block(pool, size);
    }

    return ngx_palloc_large(pool, size);
}


static void* ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    size_t       psize;
    
    ngx_pool_t  *p;
    ngx_pool_t  *new;
    ngx_pool_t  *current;

    psize = (size_t)(pool->d.end - (u_char *)pool);     　/* 内存池的大小 */

    m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log);
    if (m == NULL) 
    {
        return NULL;
    }

    new = (ngx_pool_t *)m;

    new->d.end = m + psize;
    new->d.next = NULL;
    new->d.failed = 0;

    m += sizeof(ngx_pool_data_t);
    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    new->d.last = m + size;

    current = pool->current;        /* 当前可用的内存池 */

    for (p = current; p->d.next != NULL; p = p->d.next) 
    {
        /* 失败次数+1，如果内存池的失败次数>4,则认为此内存池已不能再满足后续的内存分配需求 */ 
        if (p->d.failed++ > 4)      
        {
            current = p->d.next;    
        }
    }

    p->d.next = new;

    pool->current = current ? current : new;        /* 重新设置当前可用的内存池 */

    return m;
}



/**
*   大块内存分配
*/
static void* ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
    void              *p;
    ngx_uint_t         n;
    ngx_pool_large_t  *large;

    p = ngx_alloc(size, pool->log);
    if (p == NULL) 
    {
        return NULL;
    }

    n = 0;

    for (large = pool->large; large != NULL; large = large->next) 
    {
        if (large->alloc == NULL) 
        {
            large->alloc = p;
            
            return p;
        }

        if (n++ > 3) 
        {
            break;
        }
    }

    large = ngx_palloc(pool, sizeof(ngx_pool_large_t));
    if (large == NULL) 
    {
        ngx_free(p);
        
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


void * ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment)
{
    void              *p;
    ngx_pool_large_t  *large;

    p = ngx_memalign(alignment, size, pool->log);
    if (p == NULL) 
    {
        return NULL;
    }

    large = ngx_palloc(pool, sizeof(ngx_pool_large_t));
    if (large == NULL) 
    {
        ngx_free(p);
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}




ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t  *l;

    /* 释放大块内存 */
    for (l = pool->large; l; l = l->next) 
    {
        if (p == l->alloc) 
        {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "free: %p", l->alloc);
            
            ngx_free(l->alloc);
            l->alloc = NULL;

            return NGX_OK;
        }
    }

    return NGX_DECLINED;
}


void * ngx_pcalloc(ngx_pool_t *pool, size_t size)
{
    void *p;

    p = ngx_palloc(pool, size);
    if (p) 
    {
        ngx_memzero(p, size);   /* 置0 */
    }

    return p;
}


ngx_pool_cleanup_t * ngx_pool_cleanup_add(ngx_pool_t *p, size_t size)
{
    ngx_pool_cleanup_t  *c;

    c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
    if (c == NULL) 
    {
        return NULL;
    }

    if (size) 
    {
        c->data = ngx_palloc(p, size);
        if (c->data == NULL) 
        {
            return NULL;
        }

    } 
    else 
    {
        c->data = NULL;
    }

    c->handler = NULL;
    
    c->next = p->cleanup;
    p->cleanup = c;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, p->log, 0, "add cleanup: %p", c);

    return c;
}


void ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd)
{
    ngx_pool_cleanup_t       *c;
    ngx_pool_cleanup_file_t  *cf;

    for (c = p->cleanup; c; c = c->next) {
        if (c->handler == ngx_pool_cleanup_file) {

            cf = c->data;

            if (cf->fd == fd) {
                c->handler(cf);
                c->handler = NULL;
                return;
            }
        }
    }
}


void ngx_pool_cleanup_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d",
                   c->fd);

    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}


void ngx_pool_delete_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_err_t  err;

    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d %s",
                   c->fd, c->name);

    if (ngx_delete_file(c->name) == NGX_FILE_ERROR) {
        err = ngx_errno;

        if (err != NGX_ENOENT) {
            ngx_log_error(NGX_LOG_CRIT, c->log, err,
                          ngx_delete_file_n " \"%s\" failed", c->name);
        }
    }

    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}


#if 0

static void *
ngx_get_cached_block(size_t size)
{
    void                     *p;
    ngx_cached_block_slot_t  *slot;

    if (ngx_cycle->cache == NULL) {
        return NULL;
    }

    slot = &ngx_cycle->cache[(size + ngx_pagesize - 1) / ngx_pagesize];

    slot->tries++;

    if (slot->number) {
        p = slot->block;
        slot->block = slot->block->next;
        slot->number--;
        return p;
    }

    return NULL;
}

#endif
