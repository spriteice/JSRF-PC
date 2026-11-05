# Android

The Android app: the engine as `libmain.so` (`port/`, built with the NDK for
arm64, Android 10 and later), SDL2 for the window, the input and the sound,
and OpenGL ES 3 for the picture (`NV2A_GLES` in `src/nv2a_gl`: the same
renderer as the Windows and Linux builds, in its OpenGL ES dialect; it asks
for ES 3.0 and writes its shaders for whatever 3.x the driver gives).

| | |
|---|---|
| `java/.../SetupActivity.java` | the first screen: finds the game in the app's folder, or unpacks a disc image there (`port/xiso.c`, given the picked file as `fd:N`) |
| `java/.../JsrfActivity.java` | the game: SDL's activity, which calls the engine's `SDL_main`; the on-screen controls while no controller is connected, and the settings (⚙) |
| `java/.../TouchPadView.java` | the on-screen controls, laid out as on the iPhone (`port/ios/jsrf_ios.m`, NvTouchPad) |
| `java/.../Settings.java` | the settings file the engine shares (`scale`, `widescreen`, `touch`) |
| `../../port/android/jsrf_android.c` | the engine's side: the environment (set as the library loads), the log, the folders, the JNI for the unpacker, the controls and the settings |
| `AndroidManifest.xml`, `res/` | the app |
| `build_apk.sh` | builds it all with the SDK's own tools (no Gradle) |

Building needs the Android SDK with an NDK, build-tools and a platform, a JDK
17 or later, CMake, SDL 2.32's source (its Java side is part of the app; 2.30.0 does
not build with a recent NDK) and
the generated code (`port/recomp/gen`, see the top-level README):

```sh
JAVA_HOME=/path/to/jdk launcher/android/build_apk.sh ~/Library/Android/sdk \
    /path/to/SDL2-2.32.10 port/recomp/gen 0.4.0 out
```

Without `JSRF_KEYSTORE` (and `JSRF_KEYSTORE_PASS`, a file holding its
password) the APK is signed with a throwaway key made in the work folder.
Android only installs an update over an app signed with the same key, so a
release is always signed with the same one, which is kept out of the
repository.

The game goes in the app's folder, `Android/data/com.jumatt.jsrfunleashed/files/game`
(the extracted folder, or unpacked there from a disc image by the first
screen); its saves are kept there, under `UDATA`, and `Logs/last-run.log`
beside it. The settings are in `files/settings`.

Android-specific choices, all in `port/` and `launcher/android/`:

- `-fsigned-char`: plain `char` is signed on x86 and on Apple's arm64, where
  the engine was written and tested, and unsigned in the Arm ABI Android uses.
- `-Wl,-Bsymbolic`: the game's functions call one another directly, not
  through the PLT, as they would in an executable.
- The runtime reads some of its settings in constructors, before `main`: on
  Android they are set by a constructor of priority 101 in
  `jsrf_android.c`, which runs before the others in `libmain.so`.
- Back is trapped (`SDL_ANDROID_TRAP_BACK_BUTTON`) and is the Xbox Back
  button; the manifest keeps the old back handling
  (`enableOnBackInvokedCallback="false"`), so that Android 16 still sends it.
