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

#define CRYPTO_MAX_ALG_NAME 32
