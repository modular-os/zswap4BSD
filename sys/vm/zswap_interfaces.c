// This file define those interfaces for migrating zswap from Linux
// writed by modular-os-group.

// This file include those interfaces:
// Compress Module For Zswap (Write: Fan Yi)
// Invoker Interface for FreeBSD (Write: Yi Ran)
#include <sys/_iovec.h>

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <opencrypto/cryptodev.h>

#include "linux/highmem.h"
#include "linux/kernel.h"
#include "linux/kmod.h"
#include "sys/condvar.h"
#include "sys/mutex.h"
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

crypto_session_t
session_init_compress(struct crypto_session_params *csp)
{
	crypto_session_t sid;
	int error;
	memset(csp, 0, sizeof(struct crypto_session_params));
	csp->csp_mode = CSP_MODE_COMPRESS;
	csp->csp_cipher_alg = CRYPTO_DEFLATE_COMP;
	error = crypto_newsession(&sid, csp,
	    CRYPTOCAP_F_HARDWARE | CRYPTOCAP_F_SOFTWARE); // flags存疑
	if (error) {
		printf("crypto_newsession error: %d\n", error);
	}
	return sid;
}

struct crypto_acomp *
crypto_alloc_acomp_node(const char *alg_name, u32 type, u32 mask, int node)
{
	// 设想是一个pool一个session
	struct crypto_session_params csp;
	crypto_session_t s = session_init_compress(&csp);
	struct crypto_acomp *crp = kzalloc_node(sizeof(struct crypto_acomp),
	    GFP_KERNEL, node); // compat/linuxkpi/common/include/linux/slab.h
	crp->sid = s;
	return crp;
}
void sg_init_table(struct scatterlist* sg, int n)
{
    return;
}

void uio_set_page(struct uio* uio, struct page* page,
    unsigned int len, unsigned int offset)
{
	struct iovec *iov = kzalloc(sizeof(struct iovec), GFP_KERNEL);
	iov->iov_base = (void *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(page));
	// iov->iov_base = kmap_atomic(page);
	iov->iov_len = len;
	uio->uio_iov = iov;
	uio->uio_iovcnt = 1;
	uio->uio_offset = 0;
	uio->uio_resid = len;
	uio->uio_segflg = UIO_SYSSPACE;
	// uio_rw暂时无法设置
	return;
}

void uio_set_comp(struct uio* uio, const void* buf, unsigned int buflen)
{

	struct iovec *iov = kzalloc(sizeof(struct iovec), GFP_KERNEL);
	iov->iov_base = (void *)buf;
	iov->iov_len = buflen;
	uio->uio_iov = iov;
	uio->uio_iovcnt = 1;
	uio->uio_offset = 0;
	uio->uio_resid = buflen;
	uio->uio_segflg = UIO_SYSSPACE;
	// uio_rw暂时无法设置
	return;
}

int crypto_callback(struct cryptop* crp)
{
	pr_info("crypto finished, callback!\n");
	struct crypto_wait *ctx = (struct crypto_wait *)crp->crp_opaque;
	mtx_lock(&ctx->lock);
	ctx->completed = crp->crp_olen;
	cv_signal(&ctx->cv);
	mtx_unlock(&ctx->lock);

	if (((crp->crp_flags) & CRYPTO_F_DONE) != 0) {
		pr_info(
		    "Compress done, olen : %d, etype: %d, flags : 0x%x obuf_uio_vecAddr : %p\n",
		    crp->crp_olen, crp->crp_etype, crp->crp_flags,
		    crp->crp_obuf.cb_uio->uio_iov->iov_base);
		if (crp->crp_etype != 0) {
			ctx->completed = -crp->crp_etype;
		}
	}

	crypto_destroyreq(crp);
	return 1;
}

void acomp_request_set_params(struct acomp_req* req,
    struct uio* input,
    struct uio* output,
    unsigned int slen,
    unsigned int dlen)
{
	// 设置uio_rw
	input->uio_rw = UIO_READ;
	output->uio_rw = UIO_WRITE;
	// 设置cryptop参数
	struct cryptop *crp = kzalloc(sizeof(struct cryptop),
	    GFP_KERNEL); // linuxkpi

	crypto_initreq(crp, req->sid);
	crp->crp_flags = CRYPTO_F_CBIFSYNC; // 存疑
	crp->crp_callback = crypto_callback;
	crypto_use_uio(crp, input);
	crypto_use_output_uio(crp, output);
	crp->crp_payload_start = 0;
	// crp->crp_payload_length = max(slen, dlen);
	crp->crp_payload_length = slen;
	req->crp = crp;

	return;
}


int crypto_acomp_compress(struct acomp_req* req)
{
    req->crp->crp_op = CRYPTO_OP_COMPRESS;
    int err = crypto_dispatch(req->crp);
    return err;
}
int crypto_acomp_decompress(struct acomp_req* req)
{
	pr_info("decomp, req : %p\n", req);
	req->crp->crp_op = CRYPTO_OP_DECOMPRESS;
	int err = crypto_dispatch(req->crp);
	pr_info("decomp submit\n");
	return err;
}

int
crypto_wait_req(int err, struct crypto_wait *ctx)
{
	mtx_lock(&ctx->lock);
	pr_info("I'm waiting for crypto async compress\n");
	while (!ctx->completed) {
		cv_wait(&ctx->cv, &ctx->lock);
	}
	mtx_unlock(&ctx->lock);
	return ctx->completed;
}


/* crypto */
// void crypto_req_done(void *data, int err)
// {
//     return;
// }
// EXPORT_SYMBOL_GPL(crypto_req_done);

/*
zpool
*/
struct zpool {
	struct zpool_driver *driver;
	void *pool;
};

static LIST_HEAD(drivers_head);
static DEFINE_SPINLOCK(drivers_lock);

/**
 * zpool_register_driver() - register a zpool implementation.
 * @driver:	driver to register
 */
void zpool_register_driver(struct zpool_driver *driver)
{
	spin_lock(&drivers_lock);
	atomic_set(&driver->refcount, 0);
	list_add(&driver->list, &drivers_head);
	spin_unlock(&drivers_lock);
}
EXPORT_SYMBOL(zpool_register_driver);

/**
 * zpool_unregister_driver() - unregister a zpool implementation.
 * @driver:	driver to unregister.
 *
 * Module usage counting is used to prevent using a driver
 * while/after unloading, so if this is called from module
 * exit function, this should never fail; if called from
 * other than the module exit function, and this returns
 * failure, the driver is in use and must remain available.
 */
int zpool_unregister_driver(struct zpool_driver *driver)
{
	int ret = 0, refcount;

	spin_lock(&drivers_lock);
	refcount = atomic_read(&driver->refcount);
	WARN_ON(refcount < 0);
	if (refcount > 0)
		ret = -EBUSY;
	else
		list_del(&driver->list);
	spin_unlock(&drivers_lock);
	

	return ret;
}
EXPORT_SYMBOL(zpool_unregister_driver);

/* this assumes @type is null-terminated. */
static struct zpool_driver *zpool_get_driver(const char *type)
{
	struct zpool_driver *driver;

	spin_lock(&drivers_lock);
	list_for_each_entry(driver, &drivers_head, list) {
		if (!strcmp(driver->type, type)) {
			// bool got = try_module_get(driver->owner);

			// if (got)
			// 	atomic_inc(&driver->refcount);
            bool got = true;
			spin_unlock(&drivers_lock);
			return got ? driver : NULL;
		}
	}

	spin_unlock(&drivers_lock);
	return NULL;
}

static void zpool_put_driver(struct zpool_driver *driver)
{
	atomic_dec(&driver->refcount);
	// module_put(driver->owner);
}

/**
 * zpool_has_pool() - Check if the pool driver is available
 * @type:	The type of the zpool to check (e.g. zbud, zsmalloc)
 *
 * This checks if the @type pool driver is available.  This will try to load
 * the requested module, if needed, but there is no guarantee the module will
 * still be loaded and available immediately after calling.  If this returns
 * true, the caller should assume the pool is available, but must be prepared
 * to handle the @zpool_create_pool() returning failure.  However if this
 * returns false, the caller should assume the requested pool type is not
 * available; either the requested pool type module does not exist, or could
 * not be loaded, and calling @zpool_create_pool() with the pool type will
 * fail.
 *
 * The @type string must be null-terminated.
 *
 * Returns: true if @type pool is available, false if not
 */
bool zpool_has_pool(char *type)
{
	struct zpool_driver *driver = zpool_get_driver(type);

	if (!driver) {
		request_module("zpool-%s", type);
		driver = zpool_get_driver(type);
	}

	if (!driver)
		return false;

	zpool_put_driver(driver);
	return true;
}
EXPORT_SYMBOL(zpool_has_pool);

/**
 * zpool_create_pool() - Create a new zpool
 * @type:	The type of the zpool to create (e.g. zbud, zsmalloc)
 * @name:	The name of the zpool (e.g. zram0, zswap)
 * @gfp:	The GFP flags to use when allocating the pool.
 *
 * This creates a new zpool of the specified type.  The gfp flags will be
 * used when allocating memory, if the implementation supports it.  If the
 * ops param is NULL, then the created zpool will not be evictable.
 *
 * Implementations must guarantee this to be thread-safe.
 *
 * The @type and @name strings must be null-terminated.
 *
 * Returns: New zpool on success, NULL on failure.
 */
struct zpool *zpool_create_pool(const char *type, const char *name, gfp_t gfp)
{
	struct zpool_driver *driver;
	struct zpool *zpool;

	pr_debug("creating pool type %s\n", type);

	driver = zpool_get_driver(type);

	if (!driver) {
		request_module("zpool-%s", type);
		driver = zpool_get_driver(type);
	}

	if (!driver) {
		pr_err("no driver for type %s\n", type);
		return NULL;
	}

	zpool = kmalloc(sizeof(*zpool), gfp);
	if (!zpool) {
		pr_err("couldn't create zpool - out of memory\n");
		zpool_put_driver(driver);
		return NULL;
	}

	zpool->driver = driver;
	zpool->pool = driver->create(name, gfp);

	if (!zpool->pool) {
		pr_err("couldn't create %s pool\n", type);
		zpool_put_driver(driver);
		kfree(zpool);
		return NULL;
	}

	pr_debug("created pool type %s\n", type);

	return zpool;
}

/**
 * zpool_destroy_pool() - Destroy a zpool
 * @zpool:	The zpool to destroy.
 *
 * Implementations must guarantee this to be thread-safe,
 * however only when destroying different pools.  The same
 * pool should only be destroyed once, and should not be used
 * after it is destroyed.
 *
 * This destroys an existing zpool.  The zpool should not be in use.
 */
void zpool_destroy_pool(struct zpool *zpool)
{
	pr_debug("destroying pool type %s\n", zpool->driver->type);

	zpool->driver->destroy(zpool->pool);
	zpool_put_driver(zpool->driver);
	kfree(zpool);
}

/**
 * zpool_get_type() - Get the type of the zpool
 * @zpool:	The zpool to check
 *
 * This returns the type of the pool.
 *
 * Implementations must guarantee this to be thread-safe.
 *
 * Returns: The type of zpool.
 */
const char *zpool_get_type(struct zpool *zpool)
{
	return zpool->driver->type;
}

/**
 * zpool_malloc_support_movable() - Check if the zpool supports
 *	allocating movable memory
 * @zpool:	The zpool to check
 *
 * This returns if the zpool supports allocating movable memory.
 *
 * Implementations must guarantee this to be thread-safe.
 *
 * Returns: true if the zpool supports allocating movable memory, false if not
 */
bool zpool_malloc_support_movable(struct zpool *zpool)
{
	return zpool->driver->malloc_support_movable;
}

/**
 * zpool_malloc() - Allocate memory
 * @zpool:	The zpool to allocate from.
 * @size:	The amount of memory to allocate.
 * @gfp:	The GFP flags to use when allocating memory.
 * @handle:	Pointer to the handle to set
 *
 * This allocates the requested amount of memory from the pool.
 * The gfp flags will be used when allocating memory, if the
 * implementation supports it.  The provided @handle will be
 * set to the allocated object handle.
 *
 * Implementations must guarantee this to be thread-safe.
 *
 * Returns: 0 on success, negative value on error.
 */
int zpool_malloc(struct zpool *zpool, size_t size, gfp_t gfp,
			unsigned long *handle)
{
	return zpool->driver->zpool_malloc(zpool->pool, size, gfp, handle);
}

/**
 * zpool_free() - Free previously allocated memory
 * @zpool:	The zpool that allocated the memory.
 * @handle:	The handle to the memory to free.
 *
 * This frees previously allocated memory.  This does not guarantee
 * that the pool will actually free memory, only that the memory
 * in the pool will become available for use by the pool.
 *
 * Implementations must guarantee this to be thread-safe,
 * however only when freeing different handles.  The same
 * handle should only be freed once, and should not be used
 * after freeing.
 */
void zpool_free(struct zpool *zpool, unsigned long handle)
{
	zpool->driver->free(zpool->pool, handle);
}

/**
 * zpool_map_handle() - Map a previously allocated handle into memory
 * @zpool:	The zpool that the handle was allocated from
 * @handle:	The handle to map
 * @mapmode:	How the memory should be mapped
 *
 * This maps a previously allocated handle into memory.  The @mapmode
 * param indicates to the implementation how the memory will be
 * used, i.e. read-only, write-only, read-write.  If the
 * implementation does not support it, the memory will be treated
 * as read-write.
 *
 * This may hold locks, disable interrupts, and/or preemption,
 * and the zpool_unmap_handle() must be called to undo those
 * actions.  The code that uses the mapped handle should complete
 * its operations on the mapped handle memory quickly and unmap
 * as soon as possible.  As the implementation may use per-cpu
 * data, multiple handles should not be mapped concurrently on
 * any cpu.
 *
 * Returns: A pointer to the handle's mapped memory area.
 */
void *zpool_map_handle(struct zpool *zpool, unsigned long handle,
			enum zpool_mapmode mapmode)
{
	return zpool->driver->map(zpool->pool, handle, mapmode);
}

/**
 * zpool_unmap_handle() - Unmap a previously mapped handle
 * @zpool:	The zpool that the handle was allocated from
 * @handle:	The handle to unmap
 *
 * This unmaps a previously mapped handle.  Any locks or other
 * actions that the implementation took in zpool_map_handle()
 * will be undone here.  The memory area returned from
 * zpool_map_handle() should no longer be used after this.
 */
void zpool_unmap_handle(struct zpool *zpool, unsigned long handle)
{
	zpool->driver->unmap(zpool->pool, handle);
}

/**
 * zpool_get_total_size() - The total size of the pool
 * @zpool:	The zpool to check
 *
 * This returns the total size in bytes of the pool.
 *
 * Returns: Total size of the zpool in bytes.
 */
u64 zpool_get_total_size(struct zpool *zpool)
{
	return zpool->driver->total_size(zpool->pool);
}

/**
 * zpool_can_sleep_mapped - Test if zpool can sleep when do mapped.
 * @zpool:	The zpool to test
 *
 * Some allocators enter non-preemptible context in ->map() callback (e.g.
 * disable pagefaults) and exit that context in ->unmap(), which limits what
 * we can do with the mapped object. For instance, we cannot wait for
 * asynchronous crypto API to decompress such an object or take mutexes
 * since those will call into the scheduler. This function tells us whether
 * we use such an allocator.
 *
 * Returns: true if zpool can sleep; false otherwise.
 */
bool zpool_can_sleep_mapped(struct zpool *zpool)
{
	return zpool->driver->sleep_mapped;
}

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