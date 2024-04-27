// This file declare those interfaces for migrating zswap from Linux
// writed by modular-os-group.
#ifndef ZSWAP_INTERFACES_H
#define ZSWAP_INTERFACES_H
#include <asm/atomic.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <opencrypto/cryptodev.h>

#include "sys/mutex.h"

/* This section is some addtional defination for linuxkpi*/

// linux/kconfig.h

// linux/moduleparam.h
struct kernel_param;
struct kernel_param_ops {
	/* How the ops should behave */
	unsigned int flags;
	/* Returns 0, or -errno.  arg is in kp->arg. */
	int (*set)(const char *val, const struct kernel_param *kp);
	/* Returns length written or -errno.  Buffer is 4k (ie. be short!) */
	int (*get)(char *buffer, const struct kernel_param *kp);
	/* Optional function to free kp->arg when module unloaded. */
	void (*free)(void *arg);
};

struct kernel_param {
	const char *name;
	struct module *mod;
	const struct kernel_param_ops *ops;
	const u16 perm;
	s8 level;
	u8 flags;
	union {
		void *arg;
		const struct kparam_string *str;
		const struct kparam_array *arr;
	};
};
#define module_param_cb(name, ops, arg, perm)

int param_get_bool(char *buffer, const struct kernel_param *kp);
extern int param_set_charp(const char *val, const struct kernel_param *kp);
extern int param_get_charp(char *buffer, const struct kernel_param *kp);
extern int param_set_bool(const char *val, const struct kernel_param *kp);
// linux/mm_types.h

/*
 * A swap entry has to fit into a "unsigned long", as the entry is hidden
 * in the "index" field of the swapper address space.
 */
typedef struct {
	unsigned long val;
} swp_entry_t;

// linux/memcontrol.h

// we assert CONFIG_MEMCG_KMEM is FALSE
enum vm_event_item { ZSWPIN, ZSWPOUT };
struct obj_cgroup;
static inline void
obj_cgroup_uncharge_zswap(struct obj_cgroup *objcg, size_t size)
{
}
static inline void
obj_cgroup_put(struct obj_cgroup *objcg)
{
}
static inline struct obj_cgroup *
get_obj_cgroup_from_page(struct page *page)
{
	return NULL;
}
static inline bool
obj_cgroup_may_zswap(struct obj_cgroup *objcg)
{
	return true;
}
static inline void
obj_cgroup_charge_zswap(struct obj_cgroup *objcg, size_t size)
{
}
static inline void
count_objcg_event(struct obj_cgroup *objcg, enum vm_event_item idx)
{
}
// linux/swap.h
#define MAX_SWAPFILES (1 << 5) // original : 1 << MAX_SWAPFILES_SHIFT

// linux/mm.h

static inline unsigned long
totalram_pages(void)
{
	return 1048576;
}

// linux/swapops.h

/*
 * Extract the `offset' field from a swp_entry_t.  The swp_entry_t is in
 * arch-independent format
 */
#define SWP_OFFSET_MASK \
	((1UL << 48) - 1) // original : ((1UL << SWP_TYPE_SHIFT) - 1)
/*
 * Extract the `type' field from a swp_entry_t.  The swp_entry_t is in
 * arch-independent format
 */
static inline unsigned
swp_type(swp_entry_t entry)
{
	return 0; // original : return (entry.val >> SWP_TYPE_SHIFT);
}
static inline pgoff_t
swp_offset(swp_entry_t entry)
{
	return entry.val & SWP_OFFSET_MASK;
}
static inline swp_entry_t
swp_entry(unsigned long type, pgoff_t offset)
{
	swp_entry_t ret;
	ret.val = offset;
	return ret;
}
// linux/rculist.h

/**
 * list_first_or_null_rcu - get the first element from a list
 * @ptr:        the list head to take the element from.
 * @type:       the type of the struct this is embedded in.
 * @member:     the name of the list_head within the struct.
 *
 * Note that if the list is empty, it returns NULL.
 *
 * This primitive may safely run concurrently with the _rcu list-mutation
 * primitives such as list_add_rcu() as long as it's guarded by rcu_read_lock().
 */
#define list_first_or_null_rcu(ptr, type, member)                  \
	({                                                         \
		struct list_head *__ptr = (ptr);                   \
		struct list_head *__next = READ_ONCE(__ptr->next); \
		likely(__ptr != __next) ?                          \
		    list_entry_rcu(__next, type, member) :         \
		    NULL;                                          \
	})

// mm/internal.h

/*
 * Maximum number of reclaim retries without progress before the OOM
 * killer is consider the only way forward.
 */
#define MAX_RECLAIM_RETRIES 16

// linux/string.h
extern char *strim(char *);

static inline __must_check char *
strstrip(char *str)
{
	return strim(str);
}

enum system_states {
	SYSTEM_RUNNING,
};

extern enum system_states system_state;
static inline void *
memset_l(unsigned long *p, unsigned long v, __kernel_size_t n)
{
	if (BITS_PER_LONG == 32)
		return memset32((uint32_t *)p, v, n);
	else
		return memset64((uint64_t *)p, v, n);
}

// linux/page-flags.h

static inline int
PageTransHuge(struct page *page)
{
	return 0;
}

// linux/vm_stat.h

static inline void
count_vm_event(enum vm_event_item item)
{
}

/* This section for KCONFIGS */

#define CONFIG_ZSWAP_DEFAULT_ON true
#define CONFIG_ZSWAP_COMPRESSOR_DEFAULT "default_compressor"
#define CONFIG_ZSWAP_ZPOOL_DEFAULT "zbud"
#define CONFIG_ZSWAP_EXCLUSIVE_LOADS_DEFAULT_ON false

/* This section for zpool */
struct zpool;

/*
 * Control how a handle is mapped.  It will be ignored if the
 * implementation does not support it.  Its use is optional.
 * Note that this does not refer to memory protection, it
 * refers to how the memory will be copied in/out if copying
 * is necessary during mapping; read-write is the safest as
 * it copies the existing memory in on map, and copies the
 * changed memory back out on unmap.  Write-only does not copy
 * in the memory and should only be used for initialization.
 * If in doubt, use ZPOOL_MM_DEFAULT which is read-write.
 */
enum zpool_mapmode {
	ZPOOL_MM_RW, /* normal read-write mapping */
	ZPOOL_MM_RO, /* read-only (no copy-out at unmap time) */
	ZPOOL_MM_WO, /* write-only (no copy-in at map time) */

	ZPOOL_MM_DEFAULT = ZPOOL_MM_RW
};

bool zpool_has_pool(char *type);

struct zpool *zpool_create_pool(const char *type, const char *name, gfp_t gfp);

const char *zpool_get_type(struct zpool *pool);

void zpool_destroy_pool(struct zpool *pool);

bool zpool_malloc_support_movable(struct zpool *pool);

int zpool_malloc(struct zpool *pool, size_t size, gfp_t gfp,
    unsigned long *handle);

void zpool_free(struct zpool *pool, unsigned long handle);

void *zpool_map_handle(struct zpool *pool, unsigned long handle,
    enum zpool_mapmode mm);

void zpool_unmap_handle(struct zpool *pool, unsigned long handle);

u64 zpool_get_total_size(struct zpool *pool);

/**
 * struct zpool_driver - driver implementation for zpool
 * @type:	name of the driver.
 * @list:	entry in the list of zpool drivers.
 * @create:	create a new pool.
 * @destroy:	destroy a pool.
 * @malloc:	allocate mem from a pool.
 * @free:	free mem from a pool.
 * @sleep_mapped: whether zpool driver can sleep during map.
 * @map:	map a handle.
 * @unmap:	unmap a handle.
 * @total_size:	get total size of a pool.
 *
 * This is created by a zpool implementation and registered
 * with zpool.
 */
struct zpool_driver {
	char *type;
	struct module *owner;
	atomic_t refcount;
	struct list_head list;

	void *(*create)(const char *name, gfp_t gfp);
	void (*destroy)(void *pool);

	bool malloc_support_movable;
	int (*zpool_malloc)(void *pool, size_t size, gfp_t gfp,
	    unsigned long *handle);
	void (*free)(void *pool, unsigned long handle);

	bool sleep_mapped;
	void *(*map)(void *pool, unsigned long handle, enum zpool_mapmode mm);
	void (*unmap)(void *pool, unsigned long handle);

	u64 (*total_size)(void *pool);
};

void zpool_register_driver(struct zpool_driver *driver);

int zpool_unregister_driver(struct zpool_driver *driver);

bool zpool_can_sleep_mapped(struct zpool *pool);
// static int __init init_zbud(void);
// static void __exit exit_zbud(void);
/* This section for scatterlist */

#include <sys/uio.h>
#define scatterlist uio
#define sg_set_page uio_set_page
#define sg_init_one uio_set_comp

void sg_init_table(struct scatterlist *sg, int n);

void sg_init_table(struct scatterlist *sg, int n);
void uio_set_page(struct uio *uio, struct page *page, unsigned int len,
    unsigned int offset);
void uio_set_comp(struct uio *uio_out, const void *buf, unsigned int buflen);

/* This section for crypto */

/*
 * Miscellaneous stuff.
 */
#define CRYPTO_TFM_REQ_MAY_BACKLOG 0x00000400
#define CRYPTO_MAX_ALG_NAME 128
typedef void (*crypto_completion_t)(void *req, int err);
int crypto_callback(struct cryptop *crp);
struct crypto_acomp {
	crypto_session_t sid;
};
struct acomp_req {
	struct cryptop *crp;
	unsigned int dlen;
	crypto_session_t sid;
};
struct crypto_wait {
	struct mtx lock;
	struct cv cv;
	int completed;
};
crypto_session_t session_init_compress(struct crypto_session_params *csp);
int crypto_has_acomp(const char *alg_name, u32 type, u32 mask);
struct crypto_acomp *crypto_alloc_acomp_node(const char *alg_name, u32 type,
    u32 mask, int node);

struct acomp_req *acomp_request_alloc(struct crypto_acomp *acomp);
static inline void
crypto_free_acomp(struct crypto_acomp *tfm)
{
	kfree(tfm);
}
static inline void
crypto_init_wait(struct crypto_wait *wait)
{
	mtx_init(&wait->lock, "crypto wait lock", NULL, MTX_DEF);
	cv_init(&wait->cv, "crypto wait cv");
	wait->completed = 0;
}
static inline void
acomp_request_set_callback(struct acomp_req *req, u32 flgs,
    crypto_completion_t cmpl, void *data)
{
	return;
}
void acomp_request_set_params(struct acomp_req *req, struct uio *input,
    struct uio *output, unsigned int slen, unsigned int dlen);
int crypto_acomp_compress(struct acomp_req *req);
int crypto_acomp_decompress(struct acomp_req *req);
int crypto_wait_req(int err, struct crypto_wait *wait);

static void crypto_req_done(void *data, int err);
void
crypto_req_done(void *data, int err)
{
	return;
}
void acomp_request_free(struct acomp_req *req);

static void
param_free_charp(void *arg)
{
	return;
}

/* This section for per-cpu & cpuhp */

// we assume only one cpu !!!!
enum cpuhp_state {
	CPUHP_MM_ZSWP_MEM_PREPARE,
	CPUHP_MM_ZSWP_POOL_PREPARE,
};
#define DEFINE_PER_CPU(type, name) type name

static inline int
cpu_to_node(int cpu)
{
	return 0;
}

#define per_cpu(var, cpu) var

#define per_cpu_ptr(ptr, cpu) ptr

#define raw_cpu_ptr(ptr) ptr
#define alloc_percpu(type) (typeof(type) *)kmalloc(sizeof(type), GFP_KERNEL)
#define free_percpu(ptr) kfree(ptr)
// void free_percpu(void __percpu *ptr) {
// 	kfree(ptr);
// }
static inline int
cpuhp_state_add_instance(enum cpuhp_state state, struct hlist_node *node)
{
	return 0;
}
static inline int
cpuhp_state_remove_instance(enum cpuhp_state state, struct hlist_node *node)
{
	return 0;
}
static inline int
cpuhp_setup_state(enum cpuhp_state state, const char *name,
    int (*startup)(unsigned int cpu), int (*teardown)(unsigned int cpu))
{
	return startup(0);
}
static inline int
cpuhp_setup_state_multi(enum cpuhp_state state, const char *name,
    int (*startup)(unsigned int cpu, struct hlist_node *node),
    int (*teardown)(unsigned int cpu, struct hlist_node *node))
{
	return 0;
}

static inline void
cpuhp_remove_state(enum cpuhp_state state)
{
}
/* This Section for frontswap */

static inline void
peek(u8 *buf, int len, char *msg)
{
	printf("peek %s :\n", msg);
	for (int i = 0; i < len; i++) {
		printf("%02x ", buf[i]);
	}
	printf("\n");
}

#define late_initcall(fn) \
	SYSINIT(fn, SI_SUB_OFED_MODINIT, SI_ORDER_SEVENTH, _module_run, (fn))

#endif