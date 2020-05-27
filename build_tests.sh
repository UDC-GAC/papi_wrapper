#!/bin/bash

set -eo pipefail

# Reading arguments
testing=$1
paranoid=$2

# Install PAPI library
git clone https://bitbucket.org/icl/papi.git && cd papi/src && ./configure && make && sudo make install && cd ../..

# Set /usr/local/lib in the library path
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# CircleCI does not admit this for docker images
if [[ $paranoid = "yes" ]]; then
    sudo bash -c "echo -1 > /proc/sys/kernel/perf_event_paranoid"
fi

# Create build folder
mkdir -p build && cd build

# Configure
cmake -DCODE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug ..

# Build (for Make on Unix equivalent to `make -j $(nproc)`)
cmake --build . --config Debug -- -j1

# Whether perform testing or not
if [[ $testing = "testing" ]]; then
    # Test
    LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH ctest --output-on-failure
    LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH OMP_NUM_THREADS=1 ctest --rerun-failed
fi
