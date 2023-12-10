// This file declare those interfaces for migrating zswap from Linux
// writed by modular-os-group.

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <opencrypto/_cryptodev.h>

/* This section is some addtional defination for linuxkpi*/

// linux/types.h


#define ATOMIC_INIT(i) { (i) }

// linux/kconfig.h

#define IS_ENABLED(option) option // original : __or(IS_BUILTIN(option), IS_MODULE(option))

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
extern void param_free_charp(void *arg);
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
struct obj_cgroup;
static inline void obj_cgroup_uncharge_zswap(struct obj_cgroup *objcg,
					     size_t size) {
}
static inline void obj_cgroup_put(struct obj_cgroup *objcg) {
}
// linux/swap.h
#define MAX_SWAPFILES (1 << 5) // original : 1 << MAX_SWAPFILES_SHIFT

// linux/mm.h

static inline unsigned long totalram_pages(void)
{
	return 1000;
}

// linux/swapops.h

/*
 * Extract the `offset' field from a swp_entry_t.  The swp_entry_t is in
 * arch-independent format
 */
#define SWP_OFFSET_MASK	((1UL << 48) - 1)// original : ((1UL << SWP_TYPE_SHIFT) - 1)
/*
 * Extract the `type' field from a swp_entry_t.  The swp_entry_t is in
 * arch-independent format
 */
static inline unsigned swp_type(swp_entry_t entry)
{
	return 0; // original : return (entry.val >> SWP_TYPE_SHIFT);
}
static inline pgoff_t swp_offset(swp_entry_t entry)
{
	return entry.val & SWP_OFFSET_MASK;
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
#define list_first_or_null_rcu(ptr, type, member) \
({ \
	struct list_head *__ptr = (ptr); \
	struct list_head *__next = READ_ONCE(__ptr->next); \
	likely(__ptr != __next) ? list_entry_rcu(__next, type, member) : NULL; \
})

// mm/internal.h

/*
 * Maximum number of reclaim retries without progress before the OOM
 * killer is consider the only way forward.
 */
#define MAX_RECLAIM_RETRIES 16

// linux/string.h
extern char *strim(char *);

static inline __must_check char *strstrip(char *str)
{
	return strim(str);
}


enum system_states {
	SYSTEM_RUNNING,
} system_state;





/* This section for KCONFIGS */

#define CONFIG_ZSWAP_DEFAULT_ON true
#define CONFIG_ZSWAP_COMPRESSOR_DEFAULT "panic"
#define CONFIG_ZSWAP_ZPOOL_DEFAULT "zbud"
#define CONFIG_ZSWAP_EXCLUSIVE_LOADS_DEFAULT_ON true


/* This section for zpool */
struct zpool;

bool zpool_has_pool(char *type);
u64 zpool_get_total_size(struct zpool *pool);
void zpool_free(struct zpool *pool, unsigned long handle);
const char *zpool_get_type(struct zpool *pool);
struct zpool *zpool_create_pool(const char *type, const char *name, gfp_t gfp);
void zpool_destroy_pool(struct zpool *pool);
/* This section for crypto */

/*
 * Miscellaneous stuff.
 */
#define CRYPTO_TFM_REQ_MAY_BACKLOG	0x00000400
#define CRYPTO_MAX_ALG_NAME		128
typedef void (*crypto_completion_t)(void *req, int err);
struct crypto_acomp {
    crypto_session_t sid;
    
};
struct acomp_req {
    struct cryptop* crp;
    unsigned int dlen;
    crypto_session_t sid;
};
struct crypto_wait {
    //empty struct
};
int crypto_has_acomp(const char* alg_name, u32 type, u32 mask);
struct crypto_acomp *crypto_alloc_acomp_node(const char *alg_name, u32 type,
					u32 mask, int node);

struct acomp_req *acomp_request_alloc(struct crypto_acomp *acomp);
static inline void crypto_free_acomp(struct crypto_acomp *tfm) {
	kfree(tfm);
}
static inline void crypto_init_wait(struct crypto_wait *wait) {
	return;
}
static inline void acomp_request_set_callback(struct acomp_req *req,
					      u32 flgs,
					      crypto_completion_t cmpl,
					      void *data) {
	return;
}


static void crypto_req_done(void *data, int err);
void acomp_request_free(struct acomp_req *req);









/* This section for per-cpu & cpuhp */

// we assume only one cpu !!!! 
enum cpuhp_state {
	CPUHP_MM_ZSWP_MEM_PREPARE,
	CPUHP_MM_ZSWP_POOL_PREPARE,
};
#define DEFINE_PER_CPU(type, name) type name

static inline int cpu_to_node(int cpu) {
	return 0;
}

#define per_cpu(var, cpu) var

#define per_cpu_ptr(ptr, cpu) ptr

#define alloc_percpu(type) (typeof(type) *) kmalloc(sizeof(type), GFP_KERNEL)
void free_percpu(void __percpu *ptr);
static inline int cpuhp_state_add_instance(enum cpuhp_state state,
					   struct hlist_node *node) {
	return 0;
}
static inline int cpuhp_state_remove_instance(enum cpuhp_state state,
					      struct hlist_node *node) {
	return 0;
}