# Release process

NullPerator shares one product version across the hardware firmware, WASM
workbench, and iOS application. Release each target from a tested commit on
`nullperator-main`; do not change the project-file schema merely to publish a
new product version.

## Version sources

- `sources/ProductVersion.h` owns the user-visible product version.
- `sources/Application/Model/ProjectVersion.h` owns the persisted project
  format and compatibility policy. Change it only when the file format changes.
- The iOS build derives `CFBundleShortVersionString` from
  `sources/ProductVersion.h`.
- The iOS `CFBundleVersion` is an App Store build number. Increment it for every
  upload of the same product version.

## Prepare the release

1. Update `sources/ProductVersion.h` to the approved version.
2. Update release-facing store copy when behavior, permissions, or privacy
   answers changed.
3. Build and test every affected target.
4. Confirm the repository is clean and tag the exact tested commit.

## Required validation

### Shared host tests

```bash
cmake -S tests -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
```

### WASM workbench

```bash
tools/build-wasm.sh Release
cd web
pnpm install --frozen-lockfile
pnpm test --run
pnpm exec playwright test --workers=1
pnpm build
pnpm verify:dist
```

### NullPerator for iOS

```bash
cd web
pnpm install --frozen-lockfile
cd ..
ios/scripts/package-web.sh
xcodebuild \
  -project ios/NullPeratorIOS.xcodeproj \
  -scheme NullPeratorIOS \
  -configuration Release \
  -destination 'generic/platform=iOS' \
  build
```

Archive with the distribution team/profile, upload the build to TestFlight,
and complete the smoke test in `docs/AppStoreSubmission.md` on a real iPhone and
iPad before App Review.

### NullPerator hardware

Build with the supported ESP-IDF environment and validate on hardware:

```bash
idf.py --project-dir sources -B sources/build/node -DNode=true build
```

## Publish

1. Merge the tested release commit into `nullperator-main`.
2. Create an annotated `v<version>` tag on that commit.
3. Push the branch and tag to the product repository.
4. Publish the approved artifacts and release notes for each affected target.
5. Record any target-specific validation that remains manual.
