# zswap4BSD相关测试解释说明

## 1. 基于ycsb-redis的宏观性能测试
zswap4BSD主要进行了两个比较重要的测试，其一是基于ycsb的redis测试，测试的zswap代码在git仓库的performance_test分支。
### 1.1 环境

支持使用qemu/VMware等虚拟机环境，首先进入虚拟机环境，安装git，并拉取我们的源代码：
```bash
git clone git@github.com:modular-os/zswap4BSD.git
git checkout performance_test
```

测试zswap开启时，需要手动修改
- sys/vm/frontswap.h种的frontswap_enabled函数返回值为true、
- zbud.c中的ZBUD_PREALLOC常量为初始申请的页面数，（我们测试的时候使用了40000）
同理，测试zswap关闭时，也需要手动关闭以上两项，并重新编译内核

编译并安装内核、重启
```
make kernel -j 8 NO_CLEAN=YES && reboot
```

安装redis
```
pkg install redis
```

关闭redis的RDB和AOF持久化，这通常位于/etc/redis/redis.conf
关闭redis的严格模式，使得我们可以在外部执行ycsb，减小对性能的影响
```
appendonly yes -> appendonly no
save ""
bind 0.0.0.0
protected-mod no
```

启动redis，也可以在etc/rc.conf中设置redis的自动启动，并把虚拟机端口映射到宿主机
```
service redis onestart
```

### 1.2 测试
在宿主机上下载ycsb的预编译版本：https://github.com/brianfrankcooper/YCSB/releases/tag/0.17.0

我们patch了ycsb的coreworkload,相关patch在src/sys/vm下的core-0.18.0-SNAPSHOT.jar中，
使用时需要替换掉ycsb原来的coreworkload，并在ycsb下放入需要截取的文本文件，命名为lipsum.txt，该文件的内容可以由任意的lipsum生成器生成，也可以使用目录中的。
patch的目的是为了使redis存放的内容更符合实际业务场景下的内容，以可读的英文文本为主。

我们通过调整ycsb的workload，在workloada的读写分布(9:1)、请求分布(zipfian)的基础上，调整数据规模recordcount和operationcount，测试不同数据压力下的读写表现。

每次测试时，首先将数据装载，再运行负载
```bash
./ycsb load redis -s -P workloads/workloada -p "redis.host=your_host" -p "redis.port=your_port"
./ycsb run redis -s -P workloads/workloada -p "redis.host=your_host" -p "redis.port=your_port"
```
这之后，读取ycsb给出的性能报告，记录其中的吞吐量数据，两次测试中需要reboot freebsd确保不会互相产生影响。


## 2. 对接口进行的单元测试

接口单元测试的代码在分支test/unit_benchmark分支上，无需准备除虚拟机之外的环境

## 2.1 虚拟机环境
FreeBSD和Linux的虚拟机都使用如下脚本启动，其中file需要填写对应的镜像文件
```bash
#!/bin/sh

# 默认值
DEFAULT_MEMORY=1024
DEFAULT_CORES=1

# 从命令行参数中获取内存大小和核心数
MEMORY=${1:-$DEFAULT_MEMORY}
CORES=${2:-$DEFAULT_CORES}

# 启动QEMU
qemu-system-aarch64 \
  -machine virt \
  -bios QEMU_EFI.fd \
  -cpu cortex-a57 \
  -m "$MEMORY" \
  -smp "$CORES" \
  -nic user,hostfwd=tcp::2233-:22 \
  -drive file=freebsd.raw,format=raw,if=virtio \
  -nographic \
```

## 2.2 FreeBSD测试方法
在用户环境下编写test.c，syscall的第一个参数为数据随机比率

```c
#include <sys/syscall.h>
#include <stdio.h>
int main() {
        int a;
        a = syscall(583, ${random percnt});
        printf("called %d\n", a);
}
```

通过调整syscall的参数来确定页面的随机比率

通过执行test.c对应的二进制文件，测试相应接口的性能，要特别注意random_percent不能过高（如100），这样无论是linux还是freebsd都无法成功调用。（因为无法压缩，页面无法存放）

还可以通过调整sys/vm/zswap.c中sys_zswap_interface函数的第1559行、1572行的循环上界，来调节测试规模和测试数量。在内核重新编译并装载后，即可进行测试。

---
## 2.3 Linux测试方法
在linux，首先下载一份linux 6.5.0的内核源码
编辑linux/mm/zswap.c，通过EXPORT_SYMBOL将zswap_frontswap_load、zswap_frontswap_store两个函数符号暴露出来

```c
#include <linux/kernel.h>
int p = 100;
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include <linux/init.h>
extern int zswap_frontswap_store(unsigned type, pgoff_t offset,
                                                struct page *page);
extern int zswap_frontswap_load(unsigned type, pgoff_t offset,
                                                struct page *page, bool *exclusii
ve);

static void create_page_by_random_percent(char *virt_addr, int percent) {
    size_t total_size = PAGE_SIZE;
    size_t random_size = PAGE_SIZE * percent / 100;
    size_t same_size = total_size - random_size;

    memset(virt_addr, 'A', same_size);
    get_random_bytes(virt_addr + same_size, random_size);
}

static int perform_zswap_test(int percent) {
    unsigned type = 0;
    bool exi = false;
    struct timespec64 start_time, end_time, delta_time;
    pr_info("Start unit testing, percent: %d%%\n", percent);
    struct page *my_page = alloc_pages(GFP_KERNEL, 0);
    void *virt_addr = page_address(my_page);

    create_page_by_random_percent(virt_addr, percent);
    ktime_get_real_ts64(&start_time);
    for (int i = 0; i < 50000; i++) {
        zswap_frontswap_store(type, i, my_page);
    }
    ktime_get_real_ts64(&end_time);
    delta_time = timespec64_sub(end_time, start_time);
    pr_info("Store took %lld s, %lld ns\n", delta_time.tv_sec, delta_time.tv_nsec);

    ktime_get_real_ts64(&start_time);
    for (int i = 0; i < 100000; i++) {
        // 1 : 9 store & load
        int ifstore = (get_random_u32() % 10) < 1;

        if (ifstore) {
            zswap_frontswap_store(type, get_random_u32() % 50000, my_page);
        } else {
            zswap_frontswap_load(type, get_random_u32() % 50000, my_page, &exi);
        }
    }
    ktime_get_real_ts64(&end_time);
    delta_time = timespec64_sub(end_time, start_time);
    pr_info("Opt took %lld s, %lld ns\n", delta_time.tv_sec, delta_time.tv_nsec);

    __free_pages(my_page, 0);
    return 0;
}
static int __init zswap_test_init(void) {
    pr_info("Loading zswap_test module...\n");
    perform_zswap_test(p); // You can pass a different value here
    return 0;
}

static void __exit zswap_test_exit(void) {
    pr_info("Removing zswap_test module...\n");
}

module_init(zswap_test_init);
module_exit(zswap_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yyi");
MODULE_DESCRIPTION("Zswap performance testing module");
```

通过调整全局量p确定页面随机比率，编译后insmod test.ko的方式触发相应的测试，并在dmesg中读取相应的时间与吞吐量。并记录数据。
