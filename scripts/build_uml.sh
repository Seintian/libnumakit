#!/bin/bash
set -e

ARCH=$1
KERNEL_VERSION="6.13.5"
KERNEL_DIR="linux-$KERNEL_VERSION"

if [ -z "$ARCH" ]; then
    echo "Usage: $0 <x86_64|arm64>"
    exit 1
fi

# Map GHA arch to kernel ARCH
KARCH="um"
if [ "$ARCH" == "x86_64" ]; then
    SUBARCH="x86_64"
elif [ "$ARCH" == "arm64" ]; then
    SUBARCH="arm64"
else
    echo "Unsupported arch: $ARCH"
    exit 1
fi

echo "--- Building UML Kernel for $ARCH ($SUBARCH) ---"

# 1. Download Kernel Source if not present
if [ ! -d "$KERNEL_DIR" ]; then
    wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$KERNEL_VERSION.tar.xz
    tar -xf linux-$KERNEL_VERSION.tar.xz
fi

cd $KERNEL_DIR

# 2. Create Minimal Config
make ARCH=$KARCH SUBARCH=$SUBARCH defconfig

# Enable NUMA and NUMA Emulation
cat >> .config <<EOF
CONFIG_NUMA=y
CONFIG_NUMA_EMULATION=y
CONFIG_NODES_SHIFT=2
EOF

# Enable hostfs for easy file access
echo "CONFIG_HOSTFS=y" >> .config

# 3. Build Kernel
# We use a limited number of jobs to avoid OOM on GHA
make ARCH=$KARCH SUBARCH=$SUBARCH -j$(nproc) vmlinux

# 4. Copy binary to workspace
cp vmlinux ../vmlinux-$ARCH
cd ..

echo "--- UML Kernel Build Complete: vmlinux-$ARCH ---"
