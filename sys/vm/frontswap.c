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

#include "frontswap.h"

DEFINE_STATIC_KEY_FALSE(frontswap_enabled_key);

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
	swp_entry_t entry = {
		.val = page_private(page),
	};
	int type = swp_type(entry);
	struct swap_info_struct *sis = swap_info[type];
	pgoff_t offset = swp_offset(entry);

	VM_BUG_ON(!frontswap_ops);
	VM_BUG_ON(!PageLocked(page));
	VM_BUG_ON(sis == NULL);

	/*
	 * If a dup, we must remove the old page first; we can't leave the
	 * old page no matter if the store of the new page succeeds or fails,
	 * and we can't rely on the new page replacing the old page as we may
	 * not store to the same implementation that contains the old page.
	 */
	if (__frontswap_test(sis, offset)) {
		__frontswap_clear(sis, offset);
		frontswap_ops->invalidate_page(type, offset);
	}

	ret = frontswap_ops->store(type, offset, page);
	if (ret == 0) {
		__frontswap_set(sis, offset);
		inc_frontswap_succ_stores();
	} else {
		inc_frontswap_failed_stores();
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
	swp_entry_t entry = {
		.val = page_private(page),
	};
	int type = swp_type(entry);
	struct swap_info_struct *sis = swap_info[type];
	pgoff_t offset = swp_offset(entry);

	// TODO : Add test
	// VM_BUG_ON(!frontswap_ops);
	// VM_BUG_ON(!PageLocked(page));
	// VM_BUG_ON(sis == NULL);

	// if (!__frontswap_test(sis, offset))
	// 	return -1;

	/* Try loading from each implementation, until one succeeds. */
	ret = frontswap_ops->load(type, offset, page);
	if (ret == 0)
		inc_frontswap_loads();
	return ret;
}

/*
 * Invalidate any data from frontswap associated with the specified swaptype
 * and offset so that a subsequent "get" will fail.
 */
void
__frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
	struct swap_info_struct *sis = swap_info[type];

	VM_BUG_ON(!frontswap_ops);
	VM_BUG_ON(sis == NULL);

	if (!__frontswap_test(sis, offset))
		return;

	frontswap_ops->invalidate_page(type, offset);
	__frontswap_clear(sis, offset);
	inc_frontswap_invalidates();
}

/*
 * Invalidate all data from frontswap associated with all offsets for the
 * specified swaptype.
 */
void
__frontswap_invalidate_area(unsigned type)
{
	struct swap_info_struct *sis = swap_info[type];

	VM_BUG_ON(!frontswap_ops);
	VM_BUG_ON(sis == NULL);

	if (sis->frontswap_map == NULL)
		return;

	frontswap_ops->invalidate_area(type);
	atomic_set(&sis->frontswap_pages, 0);
	bitmap_zero(sis->frontswap_map, sis->max);
}

static int __init
init_frontswap(void)
{
	frontswap_init(0, &0);
	return 0;
}

module_init(init_frontswap);
