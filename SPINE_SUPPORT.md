# Spine support

This build uses isolated, versioned runtime backends and accepts skeleton data
exported by Spine 3.8, 4.1, 4.2, and 4.3 (`.skel` or `.json`) together with a
text `.atlas` and all texture pages referenced by that atlas. The application
detects the export version before parsing the skeleton, then loads the matching
DLL from `runtimes/SpineBackend<major><minor>.dll`.

Spine runtimes require the Editor export major/minor version to match the
runtime major/minor version. Spine 4.0 and versions not listed above therefore
produce an explicit unsupported-version error instead of being sent to a
similar but incompatible parser. The bundled GLFW library is x64, so the
Visual Studio solution intentionally provides Debug x64 and Release x64
configurations.

The executable and runtime DLLs communicate through `SpineBackendApi.h`, a
plain C ABI containing only fixed-width values, opaque handles, callbacks, and
neutral render batches. Spine runtime structures and C++ containers never
cross the DLL boundary. The 3.8, 4.1, and 4.2 DLLs adapt their respective
`spine-c` APIs; the 4.3 DLL adapts the newer `spine-c` wrapper around
`spine-cpp` and its generic render commands.

## Rendering features

- Region and weighted/unweighted mesh attachments
- Clipping attachments
- Normal, additive, multiply, and screen slot blend modes
- Skeleton, slot, and attachment color/alpha
- Two-color tinting
- Multi-page atlases and atlas texture filter/wrap settings
- Premultiplied-alpha and straight-alpha atlas textures

The **PMA atlas texture** setting must match the Texture Packer export setting.
Incorrect selection usually appears as dark or bright fringes around transparent
pixels. The choice is stored per model.

## Playback features

The runtime wrapper supports animation tracks, queued animations, default mix
duration, playback speed, skins, setup-pose reset, and Spine event callbacks.
The control panel exposes the primary animation, overlay tracks, skin, loop,
speed, mix, and PMA settings.

## Distribution

Keep the `runtimes` directory next to the executable. Each build copies the
four runtime DLLs and a license file for each backend into that directory.
Removing a backend DLL does not affect the other Spine versions or Live2D, but
models exported by that missing version will report that the matching backend
could not be loaded.

## Import ownership

Models selected outside the application assets directory are copied into a
dedicated `assets/spine/<model>` directory. Only files copied and marked as
managed by the application are eligible for deletion. Models opened directly
from an existing assets directory are removed from the registry only; their
source files are left untouched.

## Runtime licensing

Each embedded Spine runtime remains subject to the Spine Runtimes License
Agreement copied beside the backend and contained in the vendored runtime
source. Confirm the Spine Editor/runtime licensing requirements before
distributing the application.
