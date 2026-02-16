#!/bin/bash
set -e

# Configuration
IMG_URL="https://cloud-images.ubuntu.com/minimal/releases/noble/release/ubuntu-24.04-minimal-cloudimg-amd64.img"
IMG_FILE="ubuntu-24.04.img"
SEED_ISO="seed.iso"
SHARED_DIR=$(pwd)

# 1. Download Image
if [ ! -f "$IMG_FILE" ]; then
    echo "Downloading Ubuntu Cloud Image..."
    wget -q "$IMG_URL" -O "$IMG_FILE"
    qemu-img resize "$IMG_FILE" 4G
fi

# 2. Determine Acceleration
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

write_files:
  # 1. The Test Runner Script
  - path: /root/run_tests.sh
    permissions: '0755'
    content: |
      #!/bin/bash
      # Redirect ALL output to console (for CI logs) AND a log file
      exec > >(tee -a /dev/console /mnt/qemu_test.log) 2>&1
      
      echo "[VM] Starting NUMA Tests..."
      
      # Setup Environment
      mkdir -p $(dirname "$SHARED_DIR")
      ln -s /mnt "$SHARED_DIR"
      
      echo "[VM] Hardware Topology:"
      numactl --hardware
      
      # Navigate to Build Dir
      if ! cd "$SHARED_DIR/build"; then
        echo "[VM] ERROR: Build directory not found!"
        exit 1
      fi
      
      export CTEST_OUTPUT_ON_FAILURE=1
      
      # Run Tests (Excluding slow benchmarks)
      echo "[VM] Running CTest..."
      ctest -V -E benchmarks
      EXIT_CODE=\$?
      
      # Write Exit Code for Host
      echo \$EXIT_CODE > /mnt/qemu_exit_code
      echo "[VM] Finished with code: \$EXIT_CODE"
      
      # Sync to ensure data hits the host filesystem
      sync

  # 2. The Systemd Service (Defined safely here instead of via echo in runcmd)
  - path: /etc/systemd/system/numa-test.service
    permissions: '0644'
    content: |
      [Unit]
      Description=Run NUMA Tests
      After=network.target local-fs.target
      
      [Service]
      Type=oneshot
      ExecStart=/root/run_tests.sh
      # CRITICAL: Shut down the VM regardless of success or failure
      ExecStopPost=/sbin/poweroff
      StandardOutput=journal+console
      StandardError=journal+console
      
      [Install]
      WantedBy=multi-user.target

runcmd:
  # Now runcmd is simple and won't break YAML parsing
  - systemctl daemon-reload
  - systemctl enable numa-test.service
  - systemctl start numa-test.service
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
