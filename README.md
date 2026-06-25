# ElectronicsSim

## Build

```
cmake -B build
make -j -C build
```

CMakeLists.txt contains function to autodetect ZMQ and BOOST libaries if available on the host.

## Build options for CMake

```-DBOOST_ROOT=/path/boost```: set BOOST library path

```-DZMQ_ROOT=/path/zmq```: set ZMQ library path

## Cross build

```
cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-linux-gnueabihf.cmake

make -j -C build-arm
```

## Output products

- dynamic executable (ElectronicsSim) in `<build_dir>`
- static executable (ElectronicsSim.static) in `<build_dir>`
- config files `<build_dir>`
