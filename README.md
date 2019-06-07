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

#### Environment variables

- `HPT_DEBUG`: output debug log
- `HPT_INTERVAL`: interval to scan new threads and perform merging
- `HPT_SAMPLE_PERIOD`: mem sample period
- `HPT_WAKEUP_EVENT`: after WAKEUP_EVENT the data is pulled into our monitor
- `HPT_THRESHOLD`: only huge pages accessed > THRESHOLD times can be merged
