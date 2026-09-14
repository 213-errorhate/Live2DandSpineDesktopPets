# Spine support

This build embeds the Spine 3.8 C runtime and accepts skeleton data exported by
Spine 3.8 (`.skel` or `.json`) together with a text `.atlas` and all texture
pages referenced by that atlas. Other Spine major/minor data versions are
rejected with an explicit error instead of being parsed as if they were 3.8.
The bundled GLFW library is x64, so the Visual Studio solution intentionally
provides Debug x64 and Release x64 configurations.

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
The control panel exposes the primary animation, skin, loop, speed, mix, and PMA
settings.

## Import ownership

Models selected outside the application assets directory are copied into a
dedicated `assets/spine/<model>` directory. Only files copied and marked as
managed by the application are eligible for deletion. Models opened directly
from an existing assets directory are removed from the registry only; their
source files are left untouched.

## Runtime licensing

The embedded Spine runtime remains subject to the Spine Runtimes License
Agreement contained in the vendored runtime source headers. Confirm the Spine
Editor/runtime licensing requirements before distributing the application.
