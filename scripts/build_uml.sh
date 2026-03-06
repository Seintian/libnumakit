#!/bin/bash
set -e

ARCH=$1
KERNEL_VERSION="6.13.5"
KERNEL_DIR="linux-$KERNEL_VERSION"

if [ -z "$ARCH" ] || [ "$ARCH" != "x86_64" ]; then
    echo "Usage: $0 x86_64"
    echo "Note: ARM64 UML is not supported in mainline kernel yet."
    exit 1
fi

echo "--- Building UML Kernel for x86_64 ---"

# 1. Download Kernel Source if not present
if [ ! -d "$KERNEL_DIR" ]; then
    wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$KERNEL_VERSION.tar.xz
    tar -xf linux-$KERNEL_VERSION.tar.xz
fi

cd $KERNEL_DIR

# 2. Create Minimal Config
make ARCH=um SUBARCH=x86_64 defconfig

# Enable NUMA and NUMA Emulation
cat >> .config <<EOF
CONFIG_NUMA=y
CONFIG_NUMA_EMULATION=y
CONFIG_NODES_SHIFT=2
EOF

# Enable hostfs for easy file access
echo "CONFIG_HOSTFS=y" >> .config

# 3. Build Kernel
make ARCH=um SUBARCH=x86_64 -j$(nproc) vmlinux

# 4. Copy binary to workspace
cp vmlinux ../vmlinux-x86_64
cd ..

echo "--- UML Kernel Build Complete: vmlinux-x86_64 ---"
