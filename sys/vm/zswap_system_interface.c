
#include <sys/sysproto.h>

#include "linux/gfp.h"
#include "linux/types.h"
#include "sys/md5.h"
#include "vm/zswap_interfaces.h"

enum { OP_INIT = 0, OP_SWAP_STORE = 1, OP_SWAP_LOAD = 2, OP_EXIT = 3 };
int
sys_zswap_interface(struct thread *td, struct zswap_interface_args *uap)
{
	int error;
	unsigned type = 0;
	pgoff_t offset = 100;
	bool exi = false;

	MD5_CTX ctx;
	u_char digest[MD5_DIGEST_LENGTH];

	struct page *my_page = alloc_page(GFP_KERNEL);
	switch (uap->cmd) {
	case OP_INIT:
		// error = init_zbud();
		// if(error != 0) return (error);
		printf("Start Test Init In Kernel\n");
		error = zswap_init();
		zswap_frontswap_init(0);
		// check_enter_module();
		if (error != 0)
			return (error);
		break;
	case OP_SWAP_STORE:
		printf("Start Test Store In Kernel\n");
		// make a new random page
		vm_paddr_t phys_addr = VM_PAGE_TO_PHYS(my_page);
		caddr_t virt_addr = (caddr_t)PHYS_TO_DMAP(phys_addr);
		for (size_t i = 0; i < PAGE_SIZE; i += 64) {
			arc4random_buf(virt_addr + i, 16);
			memset(virt_addr + i + 16, 0, 48);
		}
		peek(virt_addr, 16, "rand buf");
		// arc4random_buf(virt_addr, PAGE_SIZE);
		// get hexdigest for the page
		MD5Init(&ctx);
		MD5Update(&ctx, virt_addr, PAGE_SIZE);
		MD5Final(digest, &ctx);
		peek(digest, MD5_DIGEST_LENGTH, "storing md5");
		int res = zswap_frontswap_store(type, offset, my_page);
		printf("store res : %d\n", res);
		memset(virt_addr, 0, PAGE_SIZE);
		break;

	case OP_SWAP_LOAD:
		// bool exi = false;
		zswap_frontswap_load(type, offset, my_page, &exi);
		vm_paddr_t phys_addr_1 = VM_PAGE_TO_PHYS(my_page);
		caddr_t virt_addr_1 = (caddr_t)PHYS_TO_DMAP(phys_addr_1);
		peek(virt_addr_1, 16, "loaded buf");
		MD5Init(&ctx);
		MD5Update(&ctx, virt_addr_1, PAGE_SIZE);
		MD5Final(digest, &ctx);
		peek(digest, MD5_DIGEST_LENGTH, "loaded md5");
		break;
	case OP_EXIT:
		break;
	}
	return 0;
}
