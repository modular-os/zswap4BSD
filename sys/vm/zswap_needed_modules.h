/* 
本文件主要实现了zswap所需要的其他模块的输出能力，以及zswap所依赖的部分宏定义等
我们主要把zswap需要的能力分为如下部分：
1. ZSWAP 所需要的一些宏，如PAGE_SIZE等
2. ZPOOL 相关的能力，我们简易的迁移了一个zbud，以实现基础的zpool能力
3. CRYPTO 相关的能力，我们简易的迁移了一个crypto，以实现基础的加密能力
4. ZSWAP 所依赖的一些Linux Kernel的其他细碎模块
*/
#include "sys/malloc.h"
#ifndef _ZSWAP_NEEDED_MODULES_
#define _ZSWAP_NEEDED_MODULES_ 1
#include <linux/kernel.h>
#include <linux/list.h>

extern struct malloc_type M_ZSWAP[];

/* NEEDED MACROS */

// zswap_pool结构所需要的当前pool对应的压缩算法名字的最大长度
#define CRYPTO_MAX_ALG_NAME 128
// 对应zswap中所使用的swap提供的swap_type, 提供了最大的zswap_tree的数量
// 在基础迁移中，我们可能完全只使用一个zswap_tree，但是在后续的优化中，我们可能会使用多个zswap_tree

#define MAX_SWAPFILES_SHIFT	5
#define SWP_DEVICE_NUM 0
#define SWP_MIGRATION_NUM 0
#define SWP_HWPOISON_NUM 0
#define SWP_PTE_MARKER_NUM 1
#define MAX_SWAPFILES \
	((1 << MAX_SWAPFILES_SHIFT) - SWP_DEVICE_NUM - \
	SWP_MIGRATION_NUM - SWP_HWPOISON_NUM - \
	SWP_PTE_MARKER_NUM)
#define BITS_PER_LONG 64
#define BITS_PER_XA_VALUE	(BITS_PER_LONG - 1)
#define SWP_TYPE_SHIFT	(BITS_PER_XA_VALUE - MAX_SWAPFILES_SHIFT)
#define SWP_OFFSET_MASK	((1UL << SWP_TYPE_SHIFT) - 1)

// vm_event_item 是用来做vm_event计数的，FreeBSD没有这种机制，我们暂且挑几个用得上的

enum vm_event_item {
	ZSWPIN,
	ZSWPOUT
};

/* CRYPTO MODULE */

#include <sys/uio.h>
#include <opencrypto/cryptodev.h>

#define scatterlist uio
#define sg_set_page uio_set_page
#define sg_init_one uio_set_comp
#define crypto_alloc_acomp_node crypto_alloc_session
struct crypto_acomp
{
    crypto_session_t sid;
    
};
struct acomp_req
{
    struct cryptop* crp;
    unsigned int dlen;
    crypto_session_t sid;
};

struct crypto_wait
{
    //empty struct
};

int crypto_callback(struct cryptop* crp);
crypto_session_t session_init_compress(struct crypto_session_params* csp);
struct crypto_acomp* crypto_alloc_session(const char* alg_name, u32 type,
    u32 mask, int node);
struct acomp_req* acomp_request_alloc(struct crypto_acomp* acomp);
void crypto_init_wait(struct crypto_wait* wait);
void acomp_request_set_callback(struct acomp_req* req,
    u32 flgs,
    void* cmpl,
    void* data);
void acomp_request_free(struct acomp_req* req);
void crypto_free_acomp(struct crypto_acomp* tfm);
int crypto_has_acomp(const char* alg_name, u32 type, u32 mask);
void sg_init_table(struct scatterlist* sg, int n);
void uio_set_page(struct uio* uio, struct page* page,
    unsigned int len, unsigned int offset);
void uio_set_comp(struct uio* uio_out,void* buf, unsigned int buflen);
void acomp_request_set_params(struct acomp_req* req,
    struct uio* input,
    struct uio* output,
    unsigned int slen,
    unsigned int dlen);
int crypto_acomp_compress(struct acomp_req* req);
int crypto_acomp_decompress(struct acomp_req* req);
int crypto_wait_req(int err, struct crypto_wait* wait);

/* ZPOOL MODULE */

/* Other Module */

/*
 * A swap entry has to fit into a "unsigned long", as the entry is hidden
 * in the "index" field of the swapper address space.
 * swp_entry_t 为 zswap_header 服务
 */
typedef struct {
	unsigned long val;
} swp_entry_t;

/*
 * swp_entry 通过 type 和 offset 来构造一个 swap entry
 */
static inline swp_entry_t swp_entry(unsigned long type, pgoff_t offset)
{
	swp_entry_t ret;

	ret.val = (type << SWP_TYPE_SHIFT) | (offset & SWP_OFFSET_MASK);
	return ret;
}

/*
 * PageTransHuge检测当前页面是否为透明大页，由于FreeBSD没有这种机制，统一返回0
 */	
static inline int PageTransHuge(struct page *page) {
	return false;
}
/*
 * Linux 中原装函数就是直接返回 NULL 和 true
 */
static inline struct obj_cgroup *get_obj_cgroup_from_page(struct page *page)
{
	return NULL;
}
static inline bool obj_cgroup_may_zswap(struct obj_cgroup *objcg)
{
	return true;
}

static inline unsigned long totalram_pages(void)
{
	return atomic_load_acq_int(&vm_cnt.v_page_count);
}
/* Count & Objcg*/
static inline void count_vm_event(enum vm_event_item item) {
    return ;
}
static inline void obj_cgroup_charge_zswap(struct obj_cgroup *objcg,
					   size_t size)
{
}
static inline void count_objcg_event(struct obj_cgroup *objcg,
				     enum vm_event_item idx)
{
}
static inline void obj_cgroup_put(struct obj_cgroup *objcg)
{
}
static inline void obj_cgroup_uncharge_zswap(struct obj_cgroup *objcg,
					     size_t size)
{
}
/* Page Functions */

struct vm_page *linux_page_to_freebsd(struct page *lpage);
struct page *freebsd_page_to_linux(struct vm_page *fpage);
/* For percpu */

#define raw_cpu_ptr(ptr) (ptr)

static inline void free_percpu(void *ptr) {
	free(ptr, M_ZSWAP);
}

enum cpuhp_state {
	CPUHP_MM_ZSWP_MEM_PREPARE,
	CPUHP_MM_ZSWP_POOL_PREPARE
};

static inline int cpuhp_state_remove_instance(enum cpuhp_state state,
					      struct hlist_node *node)
{
	return 0;
}
#endif /* _ZSWAP_NEEDED_MODULES_ */