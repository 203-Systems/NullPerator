# Node Build

This repo has a separate `node` firmware target for ESP-IDF and ESP32-S3.
It does not use the normal Pico or ADV build flow.

## Requirements

- ESP-IDF installed and exported so `idf.py` works and `IDF_PATH` is set.
- Run the commands from the `sources/` directory.
- Do not override the target: this build is fixed to `esp32s3`.

On Windows, the simplest option is to open an ESP-IDF PowerShell and then run the commands below.

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

## One-shot Build, Flash, Monitor

```bash
cd sources
idf.py -B build/node -DNode=true build flash monitor
```

## Running From Repo Root

If you prefer staying in the repo root, use `--project-dir sources` and point the build directory at `sources/build/node`:

```bash
idf.py --project-dir sources -B sources/build/node -DNode=true build flash monitor
```

## Important Notes

- `-DNode=true` switches `sources/CMakeLists.txt` into the ESP-IDF node path.
- The node CMake expects the build directory to be `build/node` relative to `sources/`.
- The node adapter requires `ESP_PLATFORM` and `IDF_TARGET=esp32s3`.
- Default config comes from `sources/Adapters/node/sdkconfig.defaults`.
- The partition table comes from `sources/Adapters/node/partitions.csv`.

## Output

ESP-IDF writes the node build output under:

```text
sources/build/node/
```

That directory will contain the app, bootloader, partition table, and other generated ESP-IDF artifacts.

## Clean Build

```bash
cd sources
idf.py -B build/node fullclean
idf.py -B build/node -DNode=true build
```

## Common Errors

`ESP_PLATFORM is not set`

- You are not running inside an ESP-IDF environment.

`Configure Node builds in '.../build/node'`

- You used the wrong `-B` path. Run from `sources/` with `-B build/node`, or from repo root with `--project-dir sources -B sources/build/node`.

`Node adapter requires IDF_TARGET='esp32s3'`

- Something overrode the target. Remove the override and rebuild.
