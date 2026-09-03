# NullPerator hardware build

The NullPerator hardware firmware uses ESP-IDF and targets ESP32-S3. Its
existing CMake selector and source paths retain the internal `Node` name for
build compatibility.

## Requirements

- ESP-IDF installed and exported so `idf.py` works and `IDF_PATH` is set.
- Run the commands from the `sources/` directory.
- Do not override the target: this build is fixed to `esp32s3`.

On Windows, open an ESP-IDF PowerShell before running these commands.

## Build

```bash
cd sources
idf.py -B build/node -DNode=true build
```

## Flash

```bash
cd sources
idf.py -B build/node -DNode=true flash
```

## Monitor

```bash
cd sources
idf.py -B build/node -DNode=true monitor
```

## Build, flash, and monitor

```bash
cd sources
idf.py -B build/node -DNode=true build flash monitor
```

## Run from the repository root

Use `--project-dir sources` and place the build output under
`sources/build/node`:

```bash
idf.py --project-dir sources -B sources/build/node -DNode=true build flash monitor
```

## Build configuration

- `-DNode=true` selects the NullPerator hardware product in
  `sources/CMakeLists.txt`.
- The build directory must be `build/node` relative to `sources/`.
- The adapter requires `ESP_PLATFORM` and `IDF_TARGET=esp32s3`.
- Default configuration comes from
  `sources/Adapters/node/sdkconfig.defaults`.
- The partition table comes from `sources/Adapters/node/partitions.csv`.

ESP-IDF writes the firmware, bootloader, partition table, and other generated
artifacts under `sources/build/node/`.

## Clean build

```bash
cd sources
idf.py -B build/node fullclean
idf.py -B build/node -DNode=true build
```

## Common errors

### `ESP_PLATFORM is not set`

ESP-IDF is not active in the current shell. Export the ESP-IDF environment and
run the command again.

### `Configure Node builds in '.../build/node'`

The build uses this existing internal error message when `-B` points to the
wrong directory. From `sources/`, use `-B build/node`. From the repository
root, use `--project-dir sources -B sources/build/node`.

### `Node adapter requires IDF_TARGET='esp32s3'`

The build uses this existing internal error message when another target was
selected. Remove the target override and rebuild.
