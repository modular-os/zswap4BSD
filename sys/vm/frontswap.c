// SPDX-License-Identifier: GPL-2.0-only
/*
 * Frontswap frontend
 *
 * This code provides the generic "frontend" layer to call a matching
 * "backend" driver implementation of frontswap.  See
 * Documentation/mm/frontswap.rst for more information.
 *
 * Copyright (C) 2009-2012 Oracle Corp.  All rights reserved.
 * Author: Dan Magenheimer
 */

#include <linux/types.h>

#include "frontswap.h"
#include "vm/swap_pager.h"
DEFINE_STATIC_KEY_FALSE(frontswap_enabled_key);

static bool
__frontswap_test(struct swap_info_struct *sis, pgoff_t offset)
{
	if (sis->frontswap_map)
		return test_bit(offset, sis->frontswap_map);
	return false;
}

static inline void
__frontswap_set(struct swap_info_struct *sis, pgoff_t offset)
{
	printf("set offset : %ld\n", offset);
	set_bit(offset, sis->frontswap_map);
}

static inline void
__frontswap_clear(struct swap_info_struct *sis, pgoff_t offset)
{
	printf("clear offset : %ld\n", offset);
	clear_bit(offset, sis->frontswap_map);
}

static const struct frontswap_ops *frontswap_ops __read_mostly;
int
frontswap_register_ops(const struct frontswap_ops *ops)
{
	if (frontswap_ops)
		return -EINVAL;

	frontswap_ops = ops;
	return 0;
}

/*
 * Called when a swap device is swapon'd.
 */
void
frontswap_init(unsigned type, unsigned long *map)
{
	// we assert only one swap device
	frontswap_ops->init(0);
}

int
__frontswap_store(struct page *page)
{
	int ret = -1;
	// swp_entry_t entry = {
	// 	.val = page_private(page),
	// };
	int type = 0;
	pgoff_t offset = swp_pager_meta_lookup(page->object, page->pindex);

	struct swdevt *sp = get_swdevt_by_page(offset);
	if (__frontswap_test(sp, offset)) {
		__frontswap_clear(sp, offset);
		frontswap_ops->invalidate_page(type, offset);
	}

	ret = frontswap_ops->store(type, offset, page);
	if (ret == 0) {
		__frontswap_set(sp, offset);
	}
	return ret;
}

/*
 * "Get" data from frontswap associated with swaptype and offset that were
 * specified when the data was put to frontswap and use it to fill the
 * specified page with data. Page must be locked and in the swap cache.
 */
int
__frontswap_load(struct page *page)
{

	int ret = -1;
	pgoff_t offset = swp_pager_meta_lookup(page->object, page->pindex);
	struct swdevt *sp = get_swdevt_by_page(offset);

	// VM_BUG_ON(!frontswap_ops);
	// VM_BUG_ON(sis == NULL);

	if (!__frontswap_test(sp, offset)) {
		printf("frontswap load failed, offset %ld not in zswap\n",
		    offset);
		return ret;
	}
	/* Try loading from each implementation, until one succeeds. */
	bool exclusive = false;
	ret = frontswap_ops->load(0, offset, page, &exclusive);
	return ret;
}

/*
 * Invalidate any data from frontswap associated with the specified swaptype
 * and offset so that a subsequent "get" will fail.
 */
void
__frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
	struct swdevt *sp = get_swdevt_by_page(offset);
	// VM_BUG_ON(!frontswap_ops);
	// VM_BUG_ON(sis == NULL);

	if (!__frontswap_test(sp, offset))
		return;

	frontswap_ops->invalidate_page(type, offset);
	__frontswap_clear(sp, offset);
	// inc_frontswap_invalidates();
}

/*
 * Invalidate all data from frontswap associated with all offsets for the
 * specified swaptype.
 */
// void
// __frontswap_invalidate_area(unsigned type)
// {
// struct swap_info_struct *sis = swap_info[type];

// VM_BUG_ON(!frontswap_ops);
// VM_BUG_ON(sis == NULL);

// 	if (sis->frontswap_map == NULL)
// 		return;

// 	frontswap_ops->invalidate_area(type);
// 	// atomic_set(&sis->frontswap_pages, 0);
// 	bitmap_zero(sis->frontswap_map, sis->max);
// }

// static int __init
// init_frontswap(void)
// {
// 	frontswap_init(0, &0);
// 	return 0;
// }

// module_init(init_frontswap);
