#!/bin/bash

set -eo pipefail

testing=$1

# Install PAPI library
git clone https://bitbucket.org/icl/papi.git && cd papi/src && ./configure && make && sudo make install && cd ../..

export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
sudo bash -c "echo -1 > /proc/sys/kernel/perf_event_paranoid"

mkdir -p build && cd build

# Configure
cmake -DCODE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug ..

# Build (for Make on Unix equivalent to `make -j $(nproc)`)
cmake --build . --config Debug -- -j1

if [[ $testing = "testing" ]]; then
    # Test
    LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH ctest --output-on-failed
    LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH OMP_NUM_THREADS=1 ctest --rerun-failed
fi
