# First Android GLES renderer slice

Chosen OpenGL ES 3 for a low-dependency first rendering checkpoint, not a final backend decision. Uses EGL pbuffer and actual GL shader compilation, clear, vertex upload, triangle draw and indexed draw via the portable D3D8 bridge, pixel readback and synchronous fences. Linux Mesa exercises real GL calls via software rendering, and CI separately compiles an Android AArch64 binary against NDK EGL/GLES libraries. Android execution remains a separate device gate.

Strict scope: one matching linear A8R8G8B8 surface, untextured pretransformed XYZRHW + diffuse triangle lists and default state. No Xbox shader translation, textures, guest depth/stencil, blending, surface copies, guest readback, window, GPU timing, or real game hooks yet. The shader is an original minimal diagnostic shader, not the game's shader. Unsupported operations stop explicitly. The context and backend must be created, used and destroyed on the same owner thread. No changes to private game code or recompilation cache.

Next gates: run the standalone diagnostic on physical Android; expand resource mapping, shader translation, render states and actual presentation; bind game-specific D3D8 HLE overrides after the device layout and ABI are verified.
