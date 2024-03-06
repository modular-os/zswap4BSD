export BASEDIR=$(pwd)
export MAKEOBJDIRPREFIX=$BASEDIR/obj
cd $BASEDIR/zswap4BSD
./tools/build/make.py --clean --cross-bindir=/usr/lib/llvm-14/bin/ TARGET=amd64 TARGET_ARCH=amd64 -n
./tools/build/make.py --clean --cross-bindir=/usr/lib/llvm-14/bin/ TARGET=amd64 TARGET_ARCH=amd64 kernel-toolchain -s -j32 -DWITH_DISK_IMAGE_TOOLS_BOOTSTRAP
./tools/build/make.py --clean --cross-bindir=/usr/lib/llvm-14/bin/ TARGET=amd64 TARGET_ARCH=amd64 KERNCONF=GENERIC NO_MODULES=yes buildkernel -s -j32