
#include<opencrypto/cryptodev.h>
#include<opencrypto/_cryptodev.h>
#include<sys/systm.h>
#include<compat/linuxkpi/common/include/linux/slab.h>
#include<compat/linuxkpi/common/include/linux/page.h>
#include<sys/uio.h>
#include<sys/_iovec.h>
#include <machine/vmparam.h>

typedef uint32_t u32;

int crypto_callback(struct cryptop* crp)
{
    if(((crp->crp_flags)& CRYPTO_F_DONE)!=0)
    return (0);
    return 1;
}

bool IS_ERR(struct crypto_acomp*acomp)
{
    return false;
}
bool IS_ERR_OR_NULL(void*ptr)
{
    return false;
}

crypto_session_t session_init_compress(struct crypto_session_params* csp)
{
    crypto_session_t sid;
    int error;
    memset(csp, 0, sizeof(struct crypto_session_params));
    csp->csp_mode = CSP_MODE_COMPRESS;
    csp->csp_cipher_alg = CRYPTO_DEFLATE_COMP;
    error = crypto_newsession(&sid, csp, CRYPTOCAP_F_HARDWARE | CRYPTOCAP_F_SOFTWARE);//flags存疑
    if (error)
    {
        printf("crypto_newsession error: %d\n", error);
    }
    return sid;
}

struct crypto_acomp* crypto_alloc_session(const char* alg_name, u32 type,
    u32 mask, int node)
{
    //设想是一个pool一个session
    struct crypto_session_params csp;
    crypto_session_t s = session_init_compress(&csp);
    struct crypto_acomp*crp= kzalloc_node(sizeof(struct crypto_acomp), GFP_KERNEL, node);//compat/linuxkpi/common/include/linux/slab.h
    crp->sid = s;
    return crp;
}


struct acomp_req* acomp_request_alloc(struct crypto_acomp* acomp)
{
    struct acomp_req*req= kzalloc(sizeof(struct crypto_req), GFP_KERNEL);
    req->sid = acomp->sid;
    return req;
}

void crypto_init_wait(struct crypto_wait* wait)
{
    return;
}
void acomp_request_set_callback(struct acomp_req* req,
    u32 flgs,
    void* cmpl,
    void* data)
{
    return;
}


void acomp_request_free(struct acomp_req* req)
{
    kfree(req);
}
void crypto_free_acomp(struct crypto_acomp* tfm)
{
    kfree(tfm);
}


int crypto_has_acomp(const char* alg_name, u32 type, u32 mask)
{
    return 0;
}


void sg_init_table(struct scatterlist* sg, int n)
{
    return;
}

void uio_set_page(struct uio* uio, struct page* page,
    unsigned int len, unsigned int offset)
{
    struct iovec iov[1];
    iov[0].iovbase = PHYS_TO_DMAP(VM_PAGE_TO_PHYS(*page));
    iov[0].iov_len = len;
    uio->uio_iov = iov;
    uio->uio_iovcnt = 1;
    uio->uio_offset = 0;
    uio->uio_resid = len;
    uio->uio_segflg = UIO_SYSSPACE;
    //uio_rw暂时无法设置
    return;
}

void uio_set_comp(struct uio* uio_out, const void* buf, unsigned int buflen)
{

    struct iovec iov[1];
    iov[0].iovbase = buf;
    iov[0].iov_len = buflen;
    uio->uio_iov = iov;
    uio->uio_iovcnt = 1;
    uio->uio_offset = 0;
    uio->uio_resid = buflen;
    uio->uio_segflg = UIO_SYSSPACE;
    //uio_rw暂时无法设置
    return;
}


void acomp_request_set_params(struct acomp_req* req,
    struct uio* input,
    struct uio* output,
    unsigned int slen,
    unsigned int dlen)
{
    //设置uio_rw
    input->uio_rw = UIO_READ;
    output->uio_rw = UIO_WRITE;
    //设置cryptop参数
    struct cryptop* crp = kzalloc(sizeof(struct cryptop), GFP_KERNEL);//linuxkpi
    crypto_initreq(crp, req->sid);
    crp->crp_flags = CRYPTO_F_CBIFSYNC| CRYPTO_F_CBIMM;//存疑
    crp->crp_callback = crypto_callback;
    crypto_use_uio(crp, input);//使用input输入
    crypto_use_output_uio(crp,output);//使用output输出
    crp->crp_payload_start = 0;
    crp->payload_length = max(slen,dlen);
    req->crp = crp;
    
    
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
