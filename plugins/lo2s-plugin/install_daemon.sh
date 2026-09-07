#!/bin/bash

bin_dir=/bin
lib_dir=/usr/lib64/

# Get lo2s running

cd /tmp

## otf2-library (if needed)
wget https://perftools.pages.jsc.fz-juelich.de/cicd/otf2/tags/otf2-3.1/otf2-3.1.tar.gz 
tar -xzf otf2-3.1.tar.gz 
cd otf2-3.1 
./configure --prefix=/usr/local 
make -j$(nproc) && make install
cp /usr/local/lib/libotf2.so* $lib_dir
ldconfig

cd /tmp
rm -rf otf2-3.1 otf2-3.1.tar.gz

## Build lo2s

mkdir lo2s-build
git clone https://github.com/tud-zih-energy/lo2s.git
cd lo2s-build
 
### Build

cmake -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}/bin \
  -DCMAKE_PREFIX_PATH=/usr/lib64 \
  -DAudit_PKG_CONFIG_INCLUDE_DIRS=/usr/include \
  -DAudit_PKG_CONFIG_LIBRARIES=audit \
  -DLibDw_PKG_CONFIG_INCLUDE_DIRS=/usr/include \
  -DLibDw_PKG_CONFIG_LIBRARIES=dw \
  -DLibBpf_PKG_CONFIG_INCLUDE_DIRS=/usr/include \
  -DLibBpf_PKG_CONFIG_LIBRARIES=bpf \
  ../lo2s
make -j$(nproc)

### Cleanup

cd /tmp
rm -rf lo2s lo2s-build


# Install Plugin

## Install Daemon

wget https://raw.githubusercontent.com/Ansgar1A4/BA_Ansgar_Gummersbach/Abgabe/plugins/lo2s-plugin/lo2s-helper/lo2d.c

gcc -o $bin_dir/lo2d lo2d.c
rm -f lo2d.c