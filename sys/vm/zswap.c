


/* Hook Point */

#include <linux/scatterlist.h>
#include "front_swap.h"
#include "sys/kassert.h"
#include "zswap_needed_modules.h"
#include <linux/rculist.h>
#include <linux/workqueue.h>
#include <linux/kref.h>

#include "linux/kernel.h"
#include "linux/rbtree.h"
#include "linux/slab.h"
#include "linux/spinlock.h"
#include "linux/highmem.h"
#include "zpool.h"

#define zswap_pool_debug(msg, p)				\
	pr_debug("%s pool %s/%s\n", msg, (p)->tfm_name,		\
		 zpool_get_type((p)->zpool))
/* zswap structs */
// struct crypto_acomp_ctx {
// 	struct crypto_acomp *acomp;
// 	struct acomp_req *req;
// 	struct crypto_wait wait;
// 	u8 *dstmem;
// 	struct mutex *mutex;
// };
struct zswap_header {
	swp_entry_t swpentry;
};
struct zswap_tree {
	struct rb_root rbroot;
	spinlock_t lock;
};
struct zswap_entry {
	struct rb_node rbnode;
	pgoff_t offset;
	int refcount;
	unsigned int length;
	struct zswap_pool *pool;
	union {
		unsigned long handle;
		unsigned long value;
	};
	struct obj_cgroup *objcg;
};
struct zswap_pool {
	struct zpool *zpool;
	struct crypto_acomp_ctx __percpu *acomp_ctx;
	struct kref kref;
	struct list_head list;
	struct work_struct release_work;
	struct work_struct shrink_work;
	struct hlist_node node;
	char tfm_name[CRYPTO_MAX_ALG_NAME];
};

/* Global Consts */
static bool zswap_enabled = IS_ENABLED(CONFIG_ZSWAP_DEFAULT_ON);
static unsigned int zswap_max_pool_percent = 20;
static unsigned int zswap_accept_thr_percent = 90;
static bool zswap_same_filled_pages_enabled = true;
static bool zswap_non_same_filled_pages_enabled = true;

/* Global Variables */
static struct workqueue_struct *shrink_wq;
static struct zswap_tree *zswap_trees[MAX_SWAPFILES];
u64 zswap_pool_total_size;
static LIST_HEAD(zswap_pools);
static u64 zswap_pool_limit_hit;
static bool zswap_pool_reached_full;
static u64 zswap_reject_kmemcache_fail;
static atomic_t zswap_same_filled_pages = ATOMIC_INIT(0);
atomic_t zswap_stored_pages = ATOMIC_INIT(0);
static bool zswap_has_pool;
static u64 zswap_reject_compress_poor;
static u64 zswap_reject_alloc_fail;
static u64 zswap_duplicate_entry;
static DEFINE_SPINLOCK(zswap_pools_lock);

static int zswap_pool_get(struct zswap_pool *pool);
static void zswap_pool_put(struct zswap_pool *pool);
static void zswap_rb_erase(struct rb_root *root, struct zswap_entry *entry);
/* Helper Functions */

static bool zswap_is_full(void)
{
	return totalram_pages() * zswap_max_pool_percent / 100 <
			DIV_ROUND_UP(zswap_pool_total_size, PAGE_SIZE);
}

static void zswap_update_total_size(void)
{
	struct zswap_pool *pool;
	u64 total = 0;

	rcu_read_lock();

	list_for_each_entry_rcu(pool, &zswap_pools, list)
		total += zpool_get_total_size(pool->zpool);

	rcu_read_unlock();

	zswap_pool_total_size = total;
}

static bool zswap_can_accept(void)
{
	return totalram_pages() * zswap_accept_thr_percent / 100 *
				zswap_max_pool_percent / 100 >
			DIV_ROUND_UP(zswap_pool_total_size, PAGE_SIZE);
}

static int zswap_is_page_same_filled(void *ptr, unsigned long *value)
{
	unsigned long *page;
	unsigned long val;
	unsigned int pos, last_pos = PAGE_SIZE / sizeof(*page) - 1;

	page = (unsigned long *)ptr;
	val = page[0];

	if (val != page[last_pos])
		return 0;

	for (pos = 1; pos < last_pos; pos++) {
		if (val != page[pos])
			return 0;
	}

	*value = val;

	return 1;
}

static void zswap_frontswap_init(unsigned type)
{
	struct zswap_tree *tree;

	tree = kzalloc(sizeof(*tree), GFP_KERNEL);
	if (!tree) {
		pr_err("alloc failed, zswap disabled for swap type %d\n", type);
		return;
	}

	tree->rbroot = RB_ROOT;
	spin_lock_init(&tree->lock);
	zswap_trees[type] = tree;
}

/* zswap entry functions */
static struct kmem_cache *zswap_entry_cache;

static struct zswap_entry *zswap_entry_cache_alloc(gfp_t gfp)
{
	struct zswap_entry *entry;
	entry = kmem_cache_alloc(zswap_entry_cache, gfp);
	if (!entry)
		return NULL;
	entry->refcount = 1;
	RB_CLEAR_NODE(&entry->rbnode);
	return entry;
}
static void zswap_entry_cache_free(struct zswap_entry *entry)
{
	kmem_cache_free(zswap_entry_cache, entry);
}
static void zswap_free_entry(struct zswap_entry *entry)
{
	// objcg MUST BE NULL
	// if (entry->objcg) {
	// 	obj_cgroup_uncharge_zswap(entry->objcg, entry->length);
	// 	obj_cgroup_put(entry->objcg);
	// }
	if (!entry->length)
		atomic_dec(&zswap_same_filled_pages);
	else {
		zpool_free(entry->pool->zpool, entry->handle);
		zswap_pool_put(entry->pool);
	}
	zswap_entry_cache_free(entry);
	atomic_dec(&zswap_stored_pages);
	zswap_update_total_size();
}
/* caller must hold the tree lock
* remove from the tree and free it, if nobody reference the entry
*/
static void zswap_entry_put(struct zswap_tree *tree,
			struct zswap_entry *entry)
{
	int refcount = --entry->refcount;

	BUG_ON(refcount < 0);
	if (refcount == 0) {
		zswap_rb_erase(&tree->rbroot, entry);
		zswap_free_entry(entry);
	}
}
/* zpool functions */
static struct zswap_pool *__zswap_pool_current(void)
{
	struct zswap_pool *pool;

	pool = list_first_or_null_rcu(&zswap_pools, typeof(*pool), list);
	WARN_ONCE(!pool && zswap_has_pool,
		  "%s: no page storage pool!\n", __func__);

	return pool;
}
static int __must_check zswap_pool_get(struct zswap_pool *pool)
{
	if (!pool)
		return 0;

	return kref_get_unless_zero(&pool->kref);
}
static struct zswap_pool *zswap_pool_current_get(void)
{
	struct zswap_pool *pool;

	rcu_read_lock();

	pool = __zswap_pool_current();
	if (!zswap_pool_get(pool))
		pool = NULL;

	rcu_read_unlock();

	return pool;
}

static struct zswap_pool *zswap_pool_current(void)
{
	assert_spin_locked(&zswap_pools_lock);

	return __zswap_pool_current();
}
static void zswap_pool_destroy(struct zswap_pool *pool)
{
	zswap_pool_debug("destroying", pool);

	// CRYPTO WAITING FOR FANYI
	// cpuhp_state_remove_instance(CPUHP_MM_ZSWP_POOL_PREPARE, &pool->node);
	// free_percpu(pool->acomp_ctx);
	zpool_destroy_pool(pool->zpool);
	kfree(pool);
}
static void __zswap_pool_release(struct work_struct *work)
{
	struct zswap_pool *pool = container_of(work, typeof(*pool),
						release_work);

	synchronize_rcu();

	/* nobody should have been able to get a kref... */
	WARN_ON(kref_get_unless_zero(&pool->kref));

	/* pool is now off zswap_pools list and has no references. */
	zswap_pool_destroy(pool);
}
static void __zswap_pool_empty(struct kref *kref)
{
	struct zswap_pool *pool;

	pool = container_of(kref, typeof(*pool), kref);

	spin_lock(&zswap_pools_lock);

	WARN_ON(pool == zswap_pool_current());

	list_del_rcu(&pool->list);

	INIT_WORK(&pool->release_work, __zswap_pool_release);
	schedule_work(&pool->release_work);

	spin_unlock(&zswap_pools_lock);
}
static void zswap_pool_put(struct zswap_pool *pool)
{
	kref_put(&pool->kref, __zswap_pool_empty);
}
static struct zswap_pool *zswap_pool_last_get(void)
{
	struct zswap_pool *pool, *last = NULL;

	rcu_read_lock();

	list_for_each_entry_rcu(pool, &zswap_pools, list)
		last = pool;
	WARN_ONCE(!last && zswap_has_pool,
		  "%s: no page storage pool!\n", __func__);
	if (!zswap_pool_get(last))
		last = NULL;

	rcu_read_unlock();

	return last;
}
/* zswap_tree (rbtree) functions */
/*
 * In the case that a entry with the same offset is found, a pointer to
 * the existing entry is stored in dupentry and the function returns -EEXIST
 */
static int zswap_rb_insert(struct rb_root *root, struct zswap_entry *entry,
			struct zswap_entry **dupentry)
{
	struct rb_node **link = &root->rb_node, *parent = NULL;
	struct zswap_entry *myentry;

	while (*link) {
		parent = *link;
		myentry = rb_entry(parent, struct zswap_entry, rbnode);
		if (myentry->offset > entry->offset)
			link = &(*link)->rb_left;
		else if (myentry->offset < entry->offset)
			link = &(*link)->rb_right;
		else {
			*dupentry = myentry;
			return -EEXIST;
		}
	}
	rb_link_node(&entry->rbnode, parent, link);
	rb_insert_color(&entry->rbnode, root);
	return 0;
}
static void zswap_rb_erase(struct rb_root *root, struct zswap_entry *entry)
{
	if (!RB_EMPTY_NODE(&entry->rbnode)) {
		rb_erase(&entry->rbnode, root);
		RB_CLEAR_NODE(&entry->rbnode);
	}
}
/*
 * Carries out the common pattern of freeing and entry's zpool allocation,
 * freeing the entry itself, and decrementing the number of stored pages.
 */

/***************************************************************/
static int zswap_frontswap_store(unsigned type, pgoff_t offset,
				struct page *page)
{
	struct zswap_tree *tree = zswap_trees[type];
	struct zswap_entry *entry, *dupentry;
	// Crypto Waiting Fanyi
	// struct scatterlist input, output;
	// struct crypto_acomp_ctx *acomp_ctx;
	struct obj_cgroup *objcg = NULL;
	struct zswap_pool *pool;
	int ret;
	unsigned int hlen, dlen = PAGE_SIZE;
	unsigned long handle, value;
	char *buf;
	u8 *src, *dst;
	struct zswap_header zhdr = { .swpentry = swp_entry(type, offset) };
	gfp_t gfp;


	/* THP isn't supported */
	if (PageTransHuge(page)) {
		ret = -EINVAL;
		goto reject;
	}

	if (!zswap_enabled || !tree) {
		ret = -ENODEV;
		goto reject;
	}

	/*
	 * XXX: zswap reclaim does not work with cgroups yet. Without a
	 * cgroup-aware entry LRU, we will push out entries system-wide based on
	 * local cgroup limits.
	 */
	objcg = get_obj_cgroup_from_page(page);
	if (objcg && !obj_cgroup_may_zswap(objcg)) {
		ret = -ENOMEM;
		goto reject;
	}

	/* reclaim space if needed */
	if (zswap_is_full()) {
		zswap_pool_limit_hit++;
		zswap_pool_reached_full = true;
		goto shrink;
	}

	if (zswap_pool_reached_full) {
	       if (!zswap_can_accept()) {
			ret = -ENOMEM;
			goto reject;
		} else
			zswap_pool_reached_full = false;
	}

	/* allocate entry */
	entry = zswap_entry_cache_alloc(GFP_KERNEL);
	if (!entry) {
		zswap_reject_kmemcache_fail++;
		ret = -ENOMEM;
		goto reject;
	}

	if (zswap_same_filled_pages_enabled) {
		src = kmap_atomic(page);
		if (zswap_is_page_same_filled(src, &value)) {
			kunmap_atomic(src);
			entry->offset = offset;
			entry->length = 0;
			entry->value = value;
			atomic_inc(&zswap_same_filled_pages);
			goto insert_entry;
		}
		kunmap_atomic(src);
	}

	if (!zswap_non_same_filled_pages_enabled) {
		ret = -EINVAL;
		goto freepage;
	}

	/* if entry is successfully added, it keeps the reference */
	entry->pool = zswap_pool_current_get();
	if (!entry->pool) {
		ret = -EINVAL;
		goto freepage;
	}

	/* Compress Waiting For Fanyi */
	/* compress */
	// acomp_ctx = raw_cpu_ptr(entry->pool->acomp_ctx);

	// mutex_lock(acomp_ctx->mutex);

	// dst = acomp_ctx->dstmem;
	// sg_init_table(&input, 1);
	// sg_set_page(&input, page, PAGE_SIZE, 0);

	// /* zswap_dstmem is of size (PAGE_SIZE * 2). Reflect same in sg_list */
	// sg_init_one(&output, dst, PAGE_SIZE * 2);
	// acomp_request_set_params(acomp_ctx->req, &input, &output, PAGE_SIZE, dlen);
	/*
	 * it maybe looks a little bit silly that we send an asynchronous request,
	 * then wait for its completion synchronously. This makes the process look
	 * synchronous in fact.
	 * Theoretically, acomp supports users send multiple acomp requests in one
	 * acomp instance, then get those requests done simultaneously. but in this
	 * case, frontswap actually does store and load page by page, there is no
	 * existing method to send the second page before the first page is done
	 * in one thread doing frontswap.
	 * but in different threads running on different cpu, we have different
	 * acomp instance, so multiple threads can do (de)compression in parallel.
	 */
	// ret = crypto_wait_req(crypto_acomp_compress(acomp_ctx->req), &acomp_ctx->wait);
	// dlen = acomp_ctx->req->dlen;

	// if (ret) {
	// 	ret = -EINVAL;
	// 	goto put_dstmem;
	// }

	/* store */
	hlen = zpool_evictable(entry->pool->zpool) ? sizeof(zhdr) : 0;
	gfp = __GFP_NORETRY | __GFP_NOWARN | __GFP_KSWAPD_RECLAIM;
	if (zpool_malloc_support_movable(entry->pool->zpool))
		gfp |= __GFP_HIGHMEM | __GFP_MOVABLE;
	ret = zpool_malloc(entry->pool->zpool, hlen + dlen, gfp, &handle);
	if (ret == -ENOSPC) {
		zswap_reject_compress_poor++;
		goto put_dstmem;
	}
	if (ret) {
		zswap_reject_alloc_fail++;
		goto put_dstmem;
	}
	buf = zpool_map_handle(entry->pool->zpool, handle, ZPOOL_MM_WO);
	memcpy(buf, &zhdr, hlen);
	memcpy(buf + hlen, dst, dlen);
	zpool_unmap_handle(entry->pool->zpool, handle);

	// CRYPTO WAITING FOR FANYI
	// mutex_unlock(acomp_ctx->mutex);

	/* populate entry */
	entry->offset = offset;
	entry->handle = handle;
	entry->length = dlen;

insert_entry:
	entry->objcg = objcg;
	// Till Now, OBJCJ MUST BE NULL
	KASSERT(entry->objcg == NULL, "Zswap objcg must NULL!");
	// if (objcg) {
	// 	obj_cgroup_charge_zswap(objcg, entry->length);
	// 	/* Account before objcg ref is moved to tree */
	// 	count_objcg_event(objcg, ZSWPOUT);
	// }

	/* map */
	spin_lock(&tree->lock);
	do {
		ret = zswap_rb_insert(&tree->rbroot, entry, &dupentry);
		if (ret == -EEXIST) {
			zswap_duplicate_entry++;
			/* remove from rbtree */
			zswap_rb_erase(&tree->rbroot, dupentry);
			zswap_entry_put(tree, dupentry);
		}
	} while (ret == -EEXIST);
	spin_unlock(&tree->lock);

	/* update stats */
	atomic_inc(&zswap_stored_pages);
	zswap_update_total_size();

	count_vm_event(ZSWPOUT);

	return 0;

put_dstmem:
	// wait for Fanyi
	// mutex_unlock(acomp_ctx->mutex);
	zswap_pool_put(entry->pool);
freepage:
	zswap_entry_cache_free(entry);
reject:
	// zswap objcg MUST NULL
	// if (objcg)
	// 	obj_cgroup_put(objcg);
	return ret;

shrink:
	pool = zswap_pool_last_get();
	if (pool)
		queue_work(shrink_wq, &pool->shrink_work);
	ret = -ENOMEM;
	goto reject;
}


static const struct frontswap_ops zswap_frontswap_ops = {
	.store = zswap_frontswap_store,
	.init = zswap_frontswap_init
};

// static int zswap_setup(void)
// {
// 	struct zswap_pool *pool;
// 	int ret;

// 	zswap_entry_cache = KMEM_CACHE(zswap_entry, 0);
// 	if (!zswap_entry_cache) {
// 		pr_err("entry cache creation failed\n");
// 		goto cache_fail;
// 	}

// 	ret = cpuhp_setup_state(CPUHP_MM_ZSWP_MEM_PREPARE, "mm/zswap:prepare",
// 				zswap_dstmem_prepare, zswap_dstmem_dead);
// 	if (ret) {
// 		pr_err("dstmem alloc failed\n");
// 		goto dstmem_fail;
// 	}

// 	ret = cpuhp_setup_state_multi(CPUHP_MM_ZSWP_POOL_PREPARE,
// 				      "mm/zswap_pool:prepare",
// 				      zswap_cpu_comp_prepare,
// 				      zswap_cpu_comp_dead);
// 	if (ret)
// 		goto hp_fail;

// 	pool = __zswap_pool_create_fallback();
// 	if (pool) {
// 		pr_info("loaded using pool %s/%s\n", pool->tfm_name,
// 			zpool_get_type(pool->zpool));
// 		list_add(&pool->list, &zswap_pools);
// 		zswap_has_pool = true;
// 	} else {
// 		pr_err("pool creation failed\n");
// 		zswap_enabled = false;
// 	}

// 	shrink_wq = create_workqueue("zswap-shrink");
// 	if (!shrink_wq)
// 		goto fallback_fail;

// 	ret = frontswap_register_ops(&zswap_frontswap_ops);
// 	if (ret)
// 		goto destroy_wq;
// 	if (zswap_debugfs_init())
// 		pr_warn("debugfs initialization failed\n");
// 	zswap_init_state = ZSWAP_INIT_SUCCEED;
// 	return 0;

// destroy_wq:
// 	destroy_workqueue(shrink_wq);
// fallback_fail:
// 	if (pool)
// 		zswap_pool_destroy(pool);
// hp_fail:
// 	cpuhp_remove_state(CPUHP_MM_ZSWP_MEM_PREPARE);
// dstmem_fail:
// 	kmem_cache_destroy(zswap_entry_cache);
// cache_fail:
// 	/* if built-in, we aren't unloaded on failure; don't allow use */
// 	zswap_init_state = ZSWAP_INIT_FAILED;
// 	zswap_enabled = false;
// 	return -ENOMEM;
// }