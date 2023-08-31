
#include "front_swap.h"


MALLOC_DEFINE(M_FRONTSWAP, "frontswap", "buffer for frontswap translate page");
static const struct frontswap_ops *frontswap_ops;
#define	EINVAL		22	/* Invalid argument */
int frontswap_register_ops(const struct frontswap_ops *ops)
{
	if (frontswap_ops)
		return -EINVAL;

	frontswap_ops = ops;
	// static_branch_inc(&frontswap_enabled_key);
	return 0;
}
// Migration from linux translate the obj to private
#define BITS_PER_LONG 64
#define BITS_PER_XA_VALUE	(BITS_PER_LONG - 1)
#define MAX_SWAPFILES_SHIFT	5
#define SWP_TYPE_SHIFT	(BITS_PER_XA_VALUE - MAX_SWAPFILES_SHIFT)
#define SWP_OFFSET_MASK	((1UL << SWP_TYPE_SHIFT) - 1)

static inline unsigned swp_type(swp_entry_t entry)
{
	return (entry.val >> SWP_TYPE_SHIFT);
}
static inline pgoff_t swp_offset(swp_entry_t entry)
{
	return entry.val & SWP_OFFSET_MASK;
}

// Linux 下，zswap用page的private拆成type和offset，共type棵树，每棵树最多有offset棵节点，也就是在swap中，用type+offset标定一个位置
// 我们需要用vm_object_t来标定一棵树，然后用index标定节点，因此，需要把vm_object_t离散化，用hash值来标定一棵树，因为64位地址空间太大，我们需要一个足够小且不冲突的hash方案
// 这里采用FreeBSD的hash实现
static unsigned long vm_object_hash(vm_object_t obj) {
	// TODO: use a better hash function
	return 0;
}

int __frontswap_store(vm_object_t obj, vm_pindex_t index, vm_page_t page)
{
	int ret = -1;
	swp_entry_t entry = { .val = vm_object_hash(obj), };
	int type = swp_type(entry);
	// struct swap_info_struct *sis = swap_info[type];
	pgoff_t offset = index;

	// ignore the following check now
	// VM_BUG_ON(!frontswap_ops);
	// VM_BUG_ON(!PageLocked(page));
	// VM_BUG_ON(sis == NULL);

	/*
	 * If a dup, we must remove the old page first; we can't leave the
	 * old page no matter if the store of the new page succeeds or fails,
	 * and we can't rely on the new page replacing the old page as we may
	 * not store to the same implementation that contains the old page.
	 */
	// if (__frontswap_test(sis, offset)) {
	// 	__frontswap_clear(sis, offset);
	// 	frontswap_ops->invalidate_page(type, offset);
	// }

	// malloc a page to translate the page
	struct page *linux_page = malloc(sizeof(struct page), M_FRONTSWAP, 0);
	translate_freebsd_page_to_linux(page, linux_page);
	ret = frontswap_ops->store(type, offset, linux_page);
	// if (ret == 0) {
	// 	__frontswap_set(sis, offset);
	// 	inc_frontswap_succ_stores();
	// } else {
	// 	inc_frontswap_failed_stores();
	// }

	return ret;
}