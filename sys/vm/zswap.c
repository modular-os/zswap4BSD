
#include <linux/kernel.h>
#include <linux/rbtree.h>
#include <linux/workqueue.h>
#include <linux/kref.h>
#include <linux/highmem.h>
#include <linux/rculist.h>
#include <linux/zpool.h>
#include "zswap_needed_modules.h"
/*
	ADDITIONAL SYMBOLS
*/
MALLOC_DEFINE(M_ZSWAP, "zswap", "Memory for Zswap module");
/* statistics */

u64 zswap_pool_total_size;

static u64 zswap_pool_limit_hit;

static bool zswap_pool_reached_full;

/* Compressed page was too big for the allocator to (optimally) store */
static u64 zswap_reject_compress_poor;
/* Store failed because underlying allocator could not get memory */
static u64 zswap_reject_alloc_fail;
/* Duplicate store was encountered (rare) */
static u64 zswap_duplicate_entry;
/* Store failed because the entry metadata could not be allocated (rare) */
static u64 zswap_reject_kmemcache_fail;

static atomic_t zswap_same_filled_pages = ATOMIC_INIT(0);
/* The number of compressed pages currently stored in zswap */
atomic_t zswap_stored_pages = ATOMIC_INIT(0);

/* protects zswap_pools list modification */
static DEFINE_SPINLOCK(zswap_pools_lock);

/* configurers */

static bool zswap_enabled = IS_ENABLED(CONFIG_ZSWAP_DEFAULT_ON);

static unsigned int zswap_max_pool_percent = 20;

static unsigned int zswap_accept_thr_percent = 90; 

/* Shrinker work queue */
static struct workqueue_struct *shrink_wq;
/*
 * Enable/disable handling same-value filled pages (enabled by default).
 * If disabled every page is considered non-same-value filled.
 */
static bool zswap_same_filled_pages_enabled = true;

/* Enable/disable handling non-same-value filled pages (enabled by default) */
static bool zswap_non_same_filled_pages_enabled = true;

/* zswap base structs */
struct crypto_acomp_ctx {
	struct crypto_acomp *acomp;
	struct acomp_req *req;
	struct crypto_wait wait;
	u8 *dstmem;
	struct mutex *mutex;
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

struct zswap_header {
	swp_entry_t swpentry;
};
/*
 * The tree lock in the zswap_tree struct protects a few things:
 * - the rbtree
 * - the refcount field of each entry in the tree
 */
struct zswap_tree {
	struct rb_root rbroot;
	spinlock_t lock;
};
/* RCU-protected iteration */
static LIST_HEAD(zswap_pools);

static struct zswap_tree *zswap_trees[MAX_SWAPFILES];

/* init completed, but couldn't create the initial pool */
static bool zswap_has_pool;

static void zswap_free_entry(struct zswap_entry *entry);
static void zswap_pool_put(struct zswap_pool *pool);
static void __zswap_pool_empty(struct kref *kref);
/*********************************
* rbtree functions
**********************************/
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
/*********************************
* helpers and fwd declarations
**********************************/
#define zswap_pool_debug(msg, p)				\
	pr_debug("%s pool %s/%s\n", msg, (p)->tfm_name,		\
		 zpool_get_type((p)->zpool))

static int zswap_pool_get(struct zswap_pool *pool);
static bool zswap_is_full(void)
{
	return totalram_pages() * zswap_max_pool_percent / 100 <
			DIV_ROUND_UP(zswap_pool_total_size, PAGE_SIZE);
}

static bool zswap_can_accept(void)
{
	return totalram_pages() * zswap_accept_thr_percent / 100 *
				zswap_max_pool_percent / 100 >
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
/*********************************
* zswap entry functions
**********************************/
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
/*
 * Carries out the common pattern of freeing and entry's zpool allocation,
 * freeing the entry itself, and decrementing the number of stored pages.
 */
static void zswap_free_entry(struct zswap_entry *entry)
{
	if (entry->objcg) {
		obj_cgroup_uncharge_zswap(entry->objcg, entry->length);
		obj_cgroup_put(entry->objcg);
	}
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

/*********************************
* pool functions
**********************************/
static struct zswap_pool *__zswap_pool_current(void)
{
	struct zswap_pool *pool;

	pool = list_first_or_null_rcu(&zswap_pools, typeof(*pool), list);
	WARN_ONCE(!pool && zswap_has_pool,
		  "%s: no page storage pool!\n", __func__);

	return pool;
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

static int __must_check zswap_pool_get(struct zswap_pool *pool)
{
	if (!pool)
		return 0;

	return kref_get_unless_zero(&pool->kref);
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

static struct zswap_pool *zswap_pool_current(void)
{
	assert_spin_locked(&zswap_pools_lock);

	return __zswap_pool_current();
}
static void zswap_pool_destroy(struct zswap_pool *pool)
{
	zswap_pool_debug("destroying", pool);

	cpuhp_state_remove_instance(CPUHP_MM_ZSWP_POOL_PREPARE, &pool->node);
	free_percpu(pool->acomp_ctx);
	zpool_destroy_pool(pool->zpool);
	kfree(pool);
}

static void zswap_pool_put(struct zswap_pool *pool)
{
	kref_put(&pool->kref, __zswap_pool_empty);
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

/*********************************
* frontswap hooks
**********************************/
/* attempts to compress and store an single page */
static int zswap_frontswap_store(unsigned type, pgoff_t offset,
				struct page *page) {
    struct zswap_tree *tree = zswap_trees[type];
	struct zswap_entry *entry, *dupentry;
	struct scatterlist input, output;
	struct crypto_acomp_ctx *acomp_ctx;
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

	/* compress */
	acomp_ctx = raw_cpu_ptr(entry->pool->acomp_ctx);

	mutex_lock(acomp_ctx->mutex);

	dst = acomp_ctx->dstmem;
	sg_init_table(&input, 1);
	sg_set_page(&input, page, PAGE_SIZE, 0);

	/* zswap_dstmem is of size (PAGE_SIZE * 2). Reflect same in sg_list */
	sg_init_one(&output, dst, PAGE_SIZE * 2);
	acomp_request_set_params(acomp_ctx->req, &input, &output, PAGE_SIZE, dlen);
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
	ret = crypto_wait_req(crypto_acomp_compress(acomp_ctx->req), &acomp_ctx->wait);
	dlen = acomp_ctx->req->dlen;

	if (ret) {
		ret = -EINVAL;
		goto put_dstmem;
	}

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
	mutex_unlock(acomp_ctx->mutex);

	/* populate entry */
	entry->offset = offset;
	entry->handle = handle;
	entry->length = dlen;

insert_entry:
	entry->objcg = objcg;
	if (objcg) {
		obj_cgroup_charge_zswap(objcg, entry->length);
		/* Account before objcg ref is moved to tree */
		count_objcg_event(objcg, ZSWPOUT);
	}

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
	mutex_unlock(acomp_ctx->mutex);
	zswap_pool_put(entry->pool);
freepage:
	zswap_entry_cache_free(entry);
reject:
	if (objcg)
		obj_cgroup_put(objcg);
	return ret;

shrink:
	pool = zswap_pool_last_get();
	if (pool)
		queue_work(shrink_wq, &pool->shrink_work);
	ret = -ENOMEM;
	goto reject;
	

}