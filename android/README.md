# Android build

This directory is a SDL2 Android wrapper around the shared C game core.

```sh
ANDROID_HOME=$HOME/android-sdk ./build-apk.sh assembleRelease
```

Requirements: JDK 17+, Android SDK platform 35, build-tools 35, NDK `27.2.12479018`.
The script downloads SDL2 2.32.10 into `android/.deps`, copies the SDL Java classes and native
sources into the Gradle project, and invokes the wrapper. Generated directories are gitignored.

The APK is built for `arm64-v8a` and `armeabi-v7a`. Release signing is enabled when
`WOD_KEYSTORE`, `WOD_KEYSTORE_PASS`, `WOD_KEY_PASS` and `WOD_KEY_ALIAS` are set.

## Touch controls

- Tap: select a unit/city or press a UI button.
- Long press: move/attack destination; keep dragging to draw a path.
- One-finger drag: box select.
- Two fingers: pan; pinch to zoom.
- City card: SCOUT / SOLDIER / HEAVY / FORTIFY buttons above the status strip.
