# UI2-only firmware acceptance

The ESP32-S3 `Node` build is currently a hybrid: UI2 owns supported LCD frames,
while the legacy Views and character renderer remain compiled as a reversible
fallback. `PICOTRACKER_UI2_DEFAULT=ON` therefore does **not** satisfy this
contract.

This acceptance gate is deliberately opt-in until a separate UI2-only product
configuration exists. It does not change any product flag.

## Complete check

Always start from a clean ESP-IDF build so stale objects cannot produce a false
result. From the repository root, the future UI2-only CI job should run:

```sh
python3 tools/verify_ui2_only_firmware.py \
  --require-complete \
  --compile-commands sources/build/node/compile_commands.json \
  --link-map sources/build/node/picoTracker.map \
  --elf sources/build/node/picoTracker.elf \
  --nm-tool xtensa-esp32s3-elf-nm
```

Exit status is `0` on success, `1` for a contract violation, and `2` for bad or
unreadable inputs. `--print-contract` prints every active rule. Supplying only
one artifact type is useful during migration but reports "partial evidence";
release acceptance must use `--require-complete`.

An opt-in CMake target can be attached by including
`cmake/Ui2OnlyFirmwareAcceptance.cmake` and calling:

```cmake
pico_tracker_add_ui2_only_acceptance_check(
  NAME ui2_only_firmware_acceptance
  FIRMWARE_TARGET picoTracker.elf
  COMPILE_COMMANDS "${CMAKE_BINARY_DIR}/compile_commands.json"
  LINK_MAP "${CMAKE_BINARY_DIR}/picoTracker.map"
  ELF "$<TARGET_FILE:picoTracker.elf>")
```

The helper target is not part of `ALL`; the UI2-only CI job must request it
explicitly after the dedicated product target exists.

## Forbidden contract

The compile database must not contain:

- `sources/Application/Views/**/*.cpp`, including legacy View, Field and modal
  implementations;
- `sources/UIFramework/**/*.cpp`;
- the mixed `sources/Application/AppWindow.cpp`;
- the legacy Node `gui/GUIWindowImp.cpp`;
- the mixed Node `display/display.c` that currently contains both character
  rendering and the RGB565 panel function.

The link map must not contain:

- `libapplication_views.a`, `libapplication_views_baseclasses.a`, or
  `libapplication_views_modaldialogs.a`;
- any `libuiframework_*.a` archive;
- the mixed `AppWindow.cpp`, `GUIWindowImp.cpp`, or `display.c` objects.

The final ELF must not define:

- `AppWindow` character buffers or legacy flush/draw methods;
- concrete/base legacy View draw methods or legacy modal/field classes;
- Node legacy window/fallback draw methods;
- character-mode `display_*` functions such as `display_putc`,
  `display_draw_changed`, or `display_draw_screen`.

This is intentionally stricter than relying on linker garbage collection.
Controller and snapshot logic must first move out of mixed legacy translation
units, and the direct RGB565 panel transport must be split from `display.c`.

## Required contract

The compile database and map must positively contain:

- `Ui2ApplicationRuntime.cpp`;
- `UiEngine.cpp`;
- `UiRgb565Presenter.cpp` and `libui2.a`.

The final ELF must positively define:

- `ui2::UiApplicationRuntime::Present`;
- `ui2::UiRgb565Presenter::Present`;
- `display_draw_rgb565_region`, or its deliberately renamed replacement after
  updating this contract in the same reviewed change.

Compile, map, and symbol checks catch different failure modes. All three are
required to prove that a release binary did not merely compile legacy code and
then hide part of it through archive selection or section garbage collection.

## Updating the lists

Changing a forbidden or required rule changes the product architecture gate.
Do not weaken a rule just to make a build pass. A rename or source split should
update the checker, its fixtures, and this document together, with the link map
and demangled symbol output attached to review.
