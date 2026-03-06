#!/bin/bash
# Inside UML, we mounted host / to guest /

# Get the path to the project root from the host environment
# Since we are in GHA, it's usually /home/runner/work/libnumakit/libnumakit
PROJECT_ROOT=$(dirname $(dirname $(readlink -f $0)))
cd $PROJECT_ROOT

export PATH=$PATH:$PROJECT_ROOT/build/bin
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$PROJECT_ROOT/build/lib

echo "--- INSIDE UML GUEST ---"
echo "Project Root: $PROJECT_ROOT"
ls -F

echo "Checking Topology..."
numactl --hardware || echo "numactl not found"

echo "Running Integration Tests..."
$PROJECT_ROOT/build/bin/nkit_integration_tests all || echo "Tests failed"

echo "--- TESTS COMPLETE ---"
# Halt UML
halt -f
