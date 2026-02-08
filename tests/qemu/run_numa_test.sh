#!/bin/bash
set -e

# Configuration
IMG_URL="https://cloud-images.ubuntu.com/minimal/releases/jammy/release/ubuntu-22.04-minimal-cloudimg-amd64.img"
IMG_FILE="ubuntu-22.04.img"
SEED_ISO="seed.iso"
SHARED_DIR=$(pwd)

# 1. Download Image (Cache check)
if [ ! -f "$IMG_FILE" ]; then
    echo "Downloading Ubuntu Cloud Image..."
    wget -q "$IMG_URL" -O "$IMG_FILE"
    # Resize to hold our build artifacts if needed, though we use virtio-fs
    qemu-img resize "$IMG_FILE" 4G
fi

# 2. Generate Cloud-Init Config (The "Script" the VM runs on boot)
cat > user-data <<EOF
#cloud-config
package_update: true
packages:
    - libnuma1
    - libhwloc15
    - libatomic1

mounts:
    - [ host0, /mnt, 9p, "trans=virtio,version=9p2000.L,cache=none" ]

runcmd:
    - echo "=========================================="
    - echo "   RUNNING TESTS IN NUMA VM (QEMU)"
    - echo "=========================================="
    - lscpu | grep NUMA
    - numactl --hardware
    - echo "------------------------------------------"
    - cd /mnt/build
    - export CTEST_OUTPUT_ON_FAILURE=1
    # Run tests and save exit code
    - ctest > /mnt/qemu_test.log 2>&1; echo \$? > /mnt/qemu_exit_code
    - poweroff
EOF

# 3. Create Seed ISO
cloud-localds "$SEED_ISO" user-data

echo "Booting QEMU with NUMA Topology (2 Nodes, 4 CPUs)..."

# 4. Boot VM
# Note: We use 2 nodes with 2 CPUs each to simulate a dual-socket server
qemu-system-x86_64 \
    -name "numa-test-vm" \
    -m 4G \
    -smp 4 \
    -cpu host -enable-kvm \
    -nographic \
    -numa node,nodeid=0,cpus=0-1,mem=2G \
    -numa node,nodeid=1,cpus=2-3,mem=2G \
    -drive "file=$IMG_FILE,format=qcow2" \
    -drive "file=$SEED_ISO,format=raw,media=cdrom" \
    -virtfs local,path="$SHARED_DIR",mount_tag=host0,security_model=mapped,id=host0 \
    -net nic,model=virtio -net user

# 5. Check Result
if [ -f qemu_exit_code ]; then
    EXIT_CODE=$(cat qemu_exit_code)
    echo "------------------------------------------"
    echo "VM Test Log:"
    cat qemu_test.log
    echo "------------------------------------------"

    if [ "$EXIT_CODE" -eq 0 ]; then
        echo "SUCCESS: Tests passed in NUMA environment."
        exit 0
    else
        echo "FAILURE: Tests failed in NUMA environment."
        exit 1
    fi
else
    echo "ERROR: VM did not write exit code (Crash?)"
    exit 1
fi
