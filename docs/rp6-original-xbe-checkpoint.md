# Physical Retroid Pocket 6 original XBE checkpoint

User-supplied local diagnostic report: original XBE parsed and mapped successfully on Android ARM64. No game bytes, generated game code or assets are included here.

| Field | Observed value |
|---|---|
| Title ID | `0x56550039` |
| Entry | `0x20A016` |
| Kernel thunk | `0x3F2BE0` |
| Sections | 27 |
| TLS | `0x426444` |
| Stack commit | `0x10000` |
| Heap reserve | `0x100000` |
| Heap commit | `0x1000` |
| Init flags | `0x5` |
| Unique kernel imports | 159 |

Observed ordinal inventory (original table order, not runtime call order):

```text
187 218 202 289 236 40 219 67 215 203 69 301 200 178 165 164 171 49 184 199 204 217 181 24 226 191 227 223 211 190 128 322 47 196 337 336 335 326 182 179 324 197 294 277 250 143 246 259 124 144 231 224 302 206 238 258 157 255 189 243 16 225 186 205 193 30 222 192 22 221 234 230 235 99 194 31 229 185 166 180 279 327 328 285 269 268 291 149 161 125 129 360 113 107 145 357 159 233 198 232 220 237 76 71 172 167 340 323 270 325 354 207 210 37 35 312 95 305 156 304 169 170 65 74 91 85 84 5 356 97 83 87 81 17 15 359 358 44 160 98 109 151 173 175 119 176 1 2 3 4 46 100 8 168 142 139 23 137 153
```

## Next acceptance gates

1. Resolve ordinals against a pinned Xbox kernel export reference, distinguishing data exports from callable functions and recording ABI signatures. Unknown entries must fail explicitly, not return fabricated success.
2. Validate the local XBE revision against upstream recompilation configuration before selecting hooks.
3. Implement and test guest startup memory, TLS, stack and heap semantics; do not interpret the inventory as a runtime trace.
4. Produce a private local recompiled module and audit unresolved symbols; do not publish generated commercial game code.
5. Validate entry dispatch and trace first actual kernel calls on Android. Implement services based on verified ABI and observed requirements.

## Limits

No ELF supplied; native function mapping not checked. Startup fields are uninitialized. No Xbox instructions, graphics or audio executed. This checkpoint is not a playable build and does not establish that all 159 imports need to execute during startup.
