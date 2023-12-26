// This file define those interfaces for migrating zswap from Linux
// writed by modular-os-group.

// This file include those interfaces:
// Compress Module For Zswap (Write: Fan Yi)
// Invoker Interface for FreeBSD (Write: Yi Ran)
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/string.h>
#include <sys/_iovec.h>
#include <opencrypto/cryptodev.h>
#include "zswap_interfaces.h"




struct acomp_req* acomp_request_alloc(struct crypto_acomp* acomp)
{
    struct acomp_req*req= kzalloc(sizeof(struct acomp_req), GFP_KERNEL);
    req->sid = acomp->sid;
    return req;
}

void acomp_request_free(struct acomp_req* req)
{
    kfree(req);
}
int crypto_has_acomp(const char* alg_name, u32 type, u32 mask)
{
    return 1;
}


void sg_init_table(struct scatterlist* sg, int n)
{
    return;
}

void uio_set_page(struct uio* uio, struct page* page,
    unsigned int len, unsigned int offset)
{
    struct iovec iov[1];
    iov[0].iov_base = (void*)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(page));
    iov[0].iov_len = len;
    uio->uio_iov = iov;
    uio->uio_iovcnt = 1;
    uio->uio_offset = 0;
    uio->uio_resid = len;
    uio->uio_segflg = UIO_SYSSPACE;
    //uio_rw暂时无法设置
    return;
}

void uio_set_comp(struct uio* uio, const void* buf, unsigned int buflen)
{

    struct iovec iov[1];
    iov[0].iov_base = (void *)buf;
    iov[0].iov_len = buflen;
    uio->uio_iov = iov;
    uio->uio_iovcnt = 1;
    uio->uio_offset = 0;
    uio->uio_resid = buflen;
    uio->uio_segflg = UIO_SYSSPACE;
    //uio_rw暂时无法设置
    return;
}



int crypto_acomp_compress(struct acomp_req* req)
{
    req->crp->crp_op = CRYPTO_OP_COMPRESS;
    int err=crypto_dispatch(req->crp);
    crypto_destroyreq(req->crp);
    return err;
}
int crypto_acomp_decompress(struct acomp_req* req)
{
    req->crp->crp_op = CRYPTO_OP_DECOMPRESS;
    int err = crypto_dispatch(req->crp);
    crypto_destroyreq(req->crp);
    return err;
}



int crypto_wait_req(int err, struct crypto_wait* wait)
{
    return err;
}


/* crypto */
// void crypto_req_done(void *data, int err)
// {
//     return;
// }
// EXPORT_SYMBOL_GPL(crypto_req_done);


// string.c

/**
 * strim - Removes leading and trailing whitespace from @s.
 * @s: The string to be stripped.
 *
 * Note that the first trailing whitespace is replaced with a %NUL-terminator
 * in the given string @s. Returns a pointer to the first non-whitespace
 * character in @s.
 */
char *strim(char *s)
{
	size_t size;
	char *end;

	size = strlen(s);
	if (!size)
		return s;

	end = s + size - 1;
	while (end >= s && isspace(*end))
		end--;
	*(end + 1) = '\0';

	return skip_spaces(s);
}

int param_set_charp(const char *val, const struct kernel_param *kp)
{
	return 0;
}
EXPORT_SYMBOL(param_set_charp);

int param_set_bool(const char *val, const struct kernel_param *kp)
{
    return 0;
	// /* No equals means "set"... */
	// if (!val) val = "1";

	// /* One of =[yYnN01] */
	// return kstrtobool(val, kp->arg);
}
EXPORT_SYMBOL(param_set_bool);

enum system_states system_state __read_mostly;