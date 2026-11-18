#!/bin/sh
# Build a signed or debug APK. Requires ANDROID_HOME with NDK 27.2.12479018.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
SDL_VERSION=${SDL_VERSION:-2.32.10}
SDL_DIR="$HERE/.deps/SDL-release-$SDL_VERSION"
JNI_SDL="$HERE/app/jni/SDL"
JAVA_DIR="$HERE/app/src/main/java"

if [ ! -d "$SDL_DIR" ]; then
    mkdir -p "$HERE/.deps"
    echo "Downloading SDL $SDL_VERSION..."
    curl -L --fail -o "$HERE/.deps/SDL2.tar.gz" \
        "https://github.com/libsdl-org/SDL/archive/refs/tags/release-$SDL_VERSION.tar.gz"
    tar xzf "$HERE/.deps/SDL2.tar.gz" -C "$HERE/.deps"
fi
rm -rf "$JNI_SDL"
cp -R "$SDL_DIR" "$JNI_SDL"
mkdir -p "$JAVA_DIR"
cp -R "$JNI_SDL/android-project/app/src/main/java/org" "$JAVA_DIR/"

cd "$HERE"
./gradlew "${1:-assembleRelease}"
