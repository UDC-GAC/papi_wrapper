#!/bin/bash

set -eo pipefail

testing=$1

mkdir -p build && cd build

# Configure
cmake -DCODE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug ..

# Build (for Make on Unix equivalent to `make -j $(nproc)`)
cmake --build . --config Debug -- -j1

if [[ $testing = "testing" ]]; then
    # Test
    ctest --output-on-failed
    OMP_NUM_THREADS=1 ctest --rerun-failed
fi
