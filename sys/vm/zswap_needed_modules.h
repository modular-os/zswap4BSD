// This file define those interfaces for migrating zswap from Linux
// writed by modular-os-group.

// This file include those interfaces:
// Compress Module For Zswap (Writer: Fan Yi)
// RBTree Interface For Zswap (Writer: Yi Ran)


#include "front_swap.h"

#define totalram_pages() zfs_totalram_pages

static inline swp_entry_t swp_entry(unsigned long type, pgoff_t offset)
{
	swp_entry_t ret;

	ret.val = (type << SWP_TYPE_SHIFT) | (offset & SWP_OFFSET_MASK);
	return ret;
}

static inline bool PageTransHuge(struct page* page) {
    return false;
}

static inline struct obj_cgroup *get_obj_cgroup_from_page(struct page *page)
{
	return NULL;
}

static inline bool obj_cgroup_may_zswap(struct obj_cgroup *objcg)
{
	return true;
}
/* ZPOOL */
enum zpool_mapmode {
	ZPOOL_MM_RW, /* normal read-write mapping */
	ZPOOL_MM_RO, /* read-only (no copy-out at unmap time) */
	ZPOOL_MM_WO, /* write-only (no copy-in at map time) */

	ZPOOL_MM_DEFAULT = ZPOOL_MM_RW
};

struct zpool;
u64 zpool_get_total_size(struct zpool *pool);
bool zpool_evictable(struct zpool *pool);
bool zpool_malloc_support_movable(struct zpool *pool);
int zpool_malloc(struct zpool *pool, size_t size, gfp_t gfp,
			unsigned long *handle);
void *zpool_map_handle(struct zpool *pool, unsigned long handle,
			enum zpool_mapmode mm);
void zpool_unmap_handle(struct zpool *pool, unsigned long handle);
void zpool_free(struct zpool *pool, unsigned long handle);
void zpool_destroy_pool(struct zpool *pool);
const char *zpool_get_type(struct zpool *pool);
/* CRYPTO */

enum vm_event_item {
	ZSWPOUT
};
void count_vm_event(enum vm_event_item);
#define CRYPTO_MAX_ALG_NAME 32


//Compress Module
#include<opencrypto/cryptodev.h>
#include<opencrypto/_cryptodev.h>
#include<compat/linuxkpi/common/include/linux/page.h>
#include<sys/uio.h>
#define scatterlist uio; 
#define sg_set_page uio_set_page;
#define sg_init_one uio_set_comp;
#define crypto_alloc_acomp_node crypto_alloc_session;
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
bool IS_ERR(struct crypto_acomp*acomp);
bool IS_ERR_OR_NULL(void*ptr);
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
void uio_set_comp(struct uio* uio_out, const void* buf, unsigned int buflen);
void acomp_request_set_params(struct acomp_req* req,
    struct uio* input,
    struct uio* output,
    unsigned int slen,
    unsigned int dlen);
int crypto_acomp_compress(struct acomp_req* req);
int crypto_acomp_decompress(struct acomp_req* req);
int crypto_wait_req(int err, struct crypto_wait* wait);
