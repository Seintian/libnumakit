#!/bin/bash
set -e

ARCH=$1
if [ -z "$ARCH" ]; then
    ARCH=$(uname -m)
fi

VMLINUX="./vmlinux-$ARCH"

if [ ! -f "$VMLINUX" ]; then
    echo "Error: $VMLINUX not found. Run scripts/build_uml.sh first."
    exit 1
fi

chmod +x scripts/uml_guest_runner.sh
chmod +x $VMLINUX

echo "--- Starting UML for $ARCH with 2 Fake NUMA Nodes ---"

# We mount host / to guest / so all tools (numactl, libc, etc) are available.
# init=... points to the guest runner script using its host path.
# We must use an absolute path for init.

INIT_SCRIPT="$(pwd)/scripts/uml_guest_runner.sh"

$VMLINUX \
    mem=512M \
    numa=fake=2 \
    rootfstype=hostfs hostfs=/ \
    init=$INIT_SCRIPT
