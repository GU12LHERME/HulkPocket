# XboxXBERecomp portable components

Unmodified upstream source files from TGP482/XboxXBERecomp, commit 767a81837fba2d86913f6af81a10b33661d35518 (MIT, see LICENSE). The crypto implementation is linked through checked guest memory adapters. GPU state and HLSL generation are compiled/tested independently as a foundation; not yet connected to a renderer or game. No generated game code, original game assets or firmware included.

The texture and vertex converters are adapted from the same pinned revision. They use a scoped checked guest-memory bridge and portable vertex-format identifiers. Texture decoding checks dimensions, input ranges, mip/face sizes and palette presence; surface copy is deliberately not implemented until synchronization and overlap semantics are handled. Vertex assembly validates stream ranges; its index pointer must be supplied by a trusted caller with count*2 bytes. These are independent CPU-side tests, not a GPU backend or game HLE integration.
