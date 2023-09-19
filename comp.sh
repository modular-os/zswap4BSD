#!/bin/bash
cd ..
mkdir obj
export BASEDIR=$(pwd)
export MAKEOBJDIRPREFIX=$BASEDIR/obj
cd $BASEDIR/src
./tools/build/make.py --debug --cross-bindir=/usr/lib/llvm-14/bin/ TARGET=amd64 TARGET_ARCH=amd64 -n
./tools/build/make.py --debug --cross-bindir=/usr/lib/llvm-14/bin/ TARGET=amd64 TARGET_ARCH=amd64 kernel-toolchain -s -j32 -DWITH_DISK_IMAGE_TOOLS_BOOTSTRAP
./tools/build/make.py --debug --cross-bindir=/usr/lib/llvm-14/bin/ TARGET=amd64 TARGET_ARCH=amd64 KERNCONF=GENERIC NO_MODULES=yes buildkernel -s -j32


dd if=/dev/zero of=/opt/share.img bs=4M count=1k
mkfs.ext4 /opt/share.img
mkdir /tmp/share
mount -o loop /opt/share.img /tmp/share
cp /root/source/freebsd/obj/root/source/freebsd/src/amd64.amd64/sys/GENERIC/kernel.full /tmp/share

mkdir -p /root/freebsd_env && cd /root/freebsd_env
wget https://download.freebsd.org/releases/VM-IMAGES/12.4-RELEASE/amd64/Latest/FreeBSD-12.4-RELEASE-amd64.qcow2.xz
unxz FreeBSD-12.4-RELEASE-amd64.qcow2.xz
qemu-img resize FreeBSD-12.4-RELEASE-amd64.qcow2 +1G
qemu-system-x86_64 -drive file=FreeBSD-12.4-RELEASE-amd64.qcow2,format=qcow2 -enable-kvm -hdb /opt/share.img
