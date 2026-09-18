# NullPerator for Android

Android host for the native tracker core, with the same bundled Svelte controls and 240 × 240 UI2 display as iOS. The app works offline; it does not load the Web UI from a server.

## Build

Requirements: JDK 17, Node.js/pnpm (see `web/package.json`), Android SDK Platform 35, Build Tools 35.0.0, NDK 28.0.13004108, and CMake 3.22.1. Android Studio can install the SDK components. Point `ANDROID_HOME` at the SDK, or set `sdk.dir` in the ignored `android/local.properties`.

From the repository root:

```sh
git submodule update --init --recursive
cd web
pnpm install --frozen-lockfile
cd ../android
./gradlew :app:assembleDebug
```

The build bundles the current Web UI automatically. Install `app/build/outputs/apk/debug/app-debug.apk` with `adb install -r`, or use Android Studio's Run action. Android 8.0 or newer is required. The configured ABIs are arm64-v8a (phones) and x86_64 (emulators).

The application version is read from `sources/ProductVersion.h`; the build number is read from the iOS target’s `CURRENT_PROJECT_VERSION`. Android and iOS therefore display the same release version and build number.

Debug builds use the standard local Android debug key. A distribution build needs your release signing configuration; no release key or credentials belong in this repository.

## Behavior

- AAudio plays the native C++ mix. WebView only renders frames and sends controls.
- Sample Import opens the system document picker, asks for a name, checks library and project name collisions, and publishes a complete WAV into `/samples`. The core then loads it through the existing import workflow.
- Recording requests microphone permission only after Record starts. Input closes when recording finishes, the app goes into the background, or audio focus is lost. No input stream is opened just by visiting Record.
- Android Back sends Shift+Left to the core, including its existing unsaved-edit confirmation.
- Backgrounding stops playback/recording and flushes settings without replacing the current project or editor. Returning to the app preserves the editing session. Explicitly save the project before closing it or terminating the app.
- Projects, samples, recordings and settings use app-private storage. Settings → Export Files saves a ZIP of persisted files through the system save dialog. Save your project before exporting. Uninstalling the app removes its private files.
- Touch and browser-supported keyboard/gamepad controls share the existing Web UI input path.

USB/Bluetooth MIDI routing is not implemented in this first Android host. It is shown as unavailable. Audio devices must support the requested 44.1 kHz shared float streams; incompatible formats fail instead of changing playback pitch or WAV timing. Device-specific latency and microphone behavior still require physical-device testing.

## Tests

The host test uses a fake AAudio API to exercise output copying, contiguous microphone blocks, capture limits, stream errors and background cleanup. It does **not** replace an Android device test.

```sh
cmake -S android/tests -B android/build/host-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build android/build/host-tests
ctest --test-dir android/build/host-tests --output-on-failure
javac --release 17 -d android/build/java-tests android/app/src/main/java/org/nullperator/app/SampleFiles.java android/tests/java/org/nullperator/app/SampleFilesTest.java
java -ea -cp android/build/java-tests org.nullperator.app.SampleFilesTest
cd web
pnpm exec vitest run tests/androidBridge.test.js tests/nativeRuntime.test.js tests/nativeAppSettings.test.js
pnpm exec playwright test android-native.spec.js native-layout.spec.js
```

Before shipping, test on a physical Android device: launch offline; play/stop each synth and a sample; import/cancel/duplicate a WAV; allow/deny mic permission; record and save; background during playback/recording; reconnect headphones; rotate the screen; verify Back warnings, project persistence and ZIP export.

## Boundaries

`app/src/main/java` owns the Activity, trusted WebView, Android permissions and document picker. Its single core HandlerThread serializes JNI calls and UI ticks. `sources/Adapters/android` supplies the native runtime, filesystem integration, framebuffer presenter, clock/timers and AAudio streams. Audio callbacks only consume/copy preallocated buffers; they do not enter Java, allocate, lock or call the tracker UI. The shared UI uses an Android Promise transport while retaining the existing WebKit bridge on iOS.
