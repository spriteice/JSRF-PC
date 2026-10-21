# The iPhone and iPad build

The same engine as the Mac's -- the recompiled game, the runtime, the Metal
renderer -- in an app of its own. Static recompilation is what makes it
possible at all: iOS lets no app generate code at run time, which rules out
every JIT and with them every Xbox emulator, and a recompiled game has nothing
to generate.

## What is different from the Mac

| | Mac | iPhone / iPad |
|---|---|---|
| renderer | Metal (OpenGL as a fallback) | Metal only: iOS has no OpenGL (`NV2A_GL_USE_NONE` in `nv2a_gl.c`) |
| window | `src/nv2a_gl/nv2a_window_mac.m` | `port/ios/jsrf_ios.m`: a UIKit scene, one view whose layer is the `CAMetalLayer` the renderer presents into |
| launcher | `launcher/JSRFLauncher.swift` asks for the game folder, sets the environment, `exec`s the engine | `jsrf_ios.m` again: one process, so it finds the game, sets the same environment and runs `host_main` on a thread of its own |
| the game | any folder, or an `.iso`, unpacked once into Application Support by the engine (`--unpack-iso`, `port/ios/xiso.c`) | the app's Documents folder; an `.iso` is unpacked by `port/ios/xiso.c` |
| input | controllers, the keyboard | controllers, on-screen controls (a stick under the left thumb, the pad's buttons under the right, Start and Back at the top), a hardware keyboard |
| saves | in the game folder, under `UDATA` (the game's own save files, where an Xbox keeps them) | the same: in the game's folder in Documents, under `UDATA` |
| cache, logs | `~/Library/Application Support/JSRF Unleashed/save` (`JSRF_SAVE_DIR`: the cache and the disk images), `last-run.log` beside the game | `Saves/` (the same role) and `Logs/` in Documents |
| signature | ad hoc, Gatekeeper's Open Anyway | ad hoc, re-signed by the sideloading tool |

Documents is what the Files app shows as *On My iPhone > JSRF Unleashed*
(`UIFileSharingEnabled`, `LSSupportsOpeningDocumentsInPlace`), and what a
Mac's Finder shows under the phone's Files tab. The game is found there, one
folder down, or two; a disc image there is offered for unpacking.

## The runtime on iOS

Mostly nothing changed, because the Mac had already brought it to an Apple
platform on arm64. What did:

- **No OpenGL.** `nv2a_gl.c` builds without its OpenGL device, its contexts
  and the few diagnostics made of OpenGL objects. The D3D8 layer (OpenGL
  through SDL2 and libepoxy, unused by a title that brings its own D3D) and
  OpenSSL (unused: the crypto is portable code) are not needed on any Apple
  build now, so a Mac build needs nothing from a package manager either.
- **No executable pages.** iOS gives an app none, and a title that commits
  memory `PAGE_EXECUTE_READWRITE` used to ask the host for exactly that. Guest
  code never runs from memory here -- it was compiled into the binary -- so
  execute permission is no longer asked for on any host.
- **No GPU from the background.** iOS refuses it. The renderer waits at the
  end of the frame while the app is away (`nv_window_wait_visible`), which
  holds the game where it was until it is back.
- **Audio interruptions.** A call or Siri stops the AudioQueue for good;
  `xa2_resume` starts it again when the app is back in front.
- **4 GB of address space.** The guest's memory map is one reservation of
  the whole 32-bit space. Recent iPhones give an app tens of gigabytes; older
  ones give less unless the app has the `extended-virtual-addressing`
  entitlement, which a free Apple ID cannot have. The release IPA asks for no
  entitlements, so that every sideloading tool takes it;
  `JSRF_IOS_ENTITLEMENTS=1 launcher/ios/package_ios.sh ...` signs with
  `launcher/ios/entitlements.plist` for an install that can have them. If the
  reservation fails, the app says so.

## Building it

Xcode with the iOS SDK. The generated code in `port/recomp/gen`, as for the
Mac. Then:

```sh
cmake -S port -B build-ios -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build-ios
launcher/ios/package_ios.sh build-ios/jsrf_recomp 0.2.0      # JSRF-Unleashed-iOS.ipa
```

The release's IPA also goes into the AltStore / SideStore source,
`altstore/source.json` (what players add in *Sources › +*), with
`launcher/ios/altstore_source.py JSRF-Unleashed-iOS.ipa --notes "..."` once it
is attached to the GitHub release; then commit the source. AltStore takes the
first entry of `versions` as the newest.

For the Simulator, `-DCMAKE_OSX_SYSROOT=iphonesimulator` and
`package_ios.sh ... --simulator`, which leaves `JSRF Unleashed.app` to install
with `xcrun simctl install`. Environment variables reach the app through
`xcrun simctl launch` as `SIMCTL_CHILD_*`: `JSRF_GAME_DIR` points it at a game
folder anywhere on the Mac, and the test hooks (`JSRF_PAD_SCRIPT`,
`JSRF_FLOW_GOTO`, ...) work as they do on the Mac. `JSRF_IOS_LOG_STDERR=1`
keeps the log on the console instead of in `Logs/`.

The signature is ad hoc -- no certificate, so no name and no team.
