# libhugepagetune
A library that performs huge page merging based on fine-grained, real-time Intel PEBS memory access traces

## Usage
### Build
Prerequiste:
- `libpfm4-dev`
- `libtbb-dev`
- `g++ >= 7`

```sh
mkdir -p build
cd build
cmake ..
make
```

### Usage
```sh
LD_PRELOAD="./libhugepagetune.so" any-app
```
