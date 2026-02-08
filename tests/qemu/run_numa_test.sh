#!/bin/bash
set -e

# Configuration
IMG_URL="https://cloud-images.ubuntu.com/minimal/releases/noble/release/ubuntu-24.04-minimal-cloudimg-amd64.img"
IMG_FILE="ubuntu-24.04.img"
SEED_ISO="seed.iso"
SHARED_DIR=$(pwd) # This is the absolute path on the HOST (e.g., /home/runner/work/...)

# 1. Download Image
if [ ! -f "$IMG_FILE" ]; then
    echo "Downloading Ubuntu Cloud Image..."
    wget -q "$IMG_URL" -O "$IMG_FILE"
    qemu-img resize "$IMG_FILE" 4G
fi

# 2. Determine Acceleration Strategy
if [ -e /dev/kvm ] && sudo chmod 666 /dev/kvm 2>/dev/null; then
    echo "KVM available. Using Hardware Acceleration."
    QEMU_CPU="-cpu host"
    QEMU_ACCEL="-enable-kvm"
else
    echo "WARNING: KVM not found. Using Software Emulation (Slow)."
    QEMU_CPU="-cpu max"
    QEMU_ACCEL=""
fi

# 3. Generate Cloud-Init Config
# We create a symlink so the VM sees the files at the exact same path as the Host.
cat > user-data <<EOF
#cloud-config
package_update: true
packages:
  - libnuma1
  - numactl
  - libhwloc15
  - libatomic1
  - cmake

mounts:
  - [ host0, /mnt, 9p, "trans=virtio,version=9p2000.L,cache=none" ]

runcmd:
  - echo "=========================================="
  - echo "   RUNNING TESTS IN NUMA VM (QEMU)"
  - echo "=========================================="
  # 1. Recreate Host Directory Structure
  # We strip the project folder name to get the parent path
  - mkdir -p $(dirname "$SHARED_DIR")
  
  # 2. Create the Symlink
  # /home/runner/work/libnumakit/libnumakit -> /mnt
  - ln -s /mnt "$SHARED_DIR"
  
  - echo "Path Masquerading Active:"
  - ls -ld "$SHARED_DIR"
  
  - echo "------------------------------------------"
  - lscpu | grep NUMA
  - numactl --hardware
  - echo "------------------------------------------"
  
  # 3. Run Tests (Now using the masked path)
  # We enter the directory using the HOST path, which now resolves to /mnt/build
  - cd "$SHARED_DIR/build"
  
  - export CTEST_OUTPUT_ON_FAILURE=1
  - ctest > /mnt/qemu_test.log 2>&1; echo \$? > /mnt/qemu_exit_code
  - poweroff
EOF

# 4. Create Seed ISO
cloud-localds "$SEED_ISO" user-data

echo "Booting QEMU with NUMA Topology (2 Nodes, 4 CPUs)..."

# 5. Boot VM
qemu-system-x86_64 \
    -name "numa-test-vm" \
    $QEMU_ACCEL \
    $QEMU_CPU \
    -m 4G \
    -smp 4 \
    -nographic \
    -object memory-backend-ram,id=mem0,size=2G \
    -object memory-backend-ram,id=mem1,size=2G \
    -numa node,nodeid=0,cpus=0-1,memdev=mem0 \
    -numa node,nodeid=1,cpus=2-3,memdev=mem1 \
    -drive "file=$IMG_FILE,format=qcow2" \
    -drive "file=$SEED_ISO,format=raw,media=cdrom" \
    -virtfs local,path="$SHARED_DIR",mount_tag=host0,security_model=mapped,id=host0 \
    -net nic,model=virtio -net user

# 6. Check Result
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
