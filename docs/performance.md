# Performance measurement

## Reproducible workload

Run on an otherwise idle graphics adapter in a release build:

```sh
cargo test --release --locked --test performance -- --ignored --nocapture
```

The headless benchmark uses the default 1,024-voxel world and 1280×720 interactive rendering, including the presentation pass. It measures two stationary cameras: an elevated view at `(512, 1026, 512)` with rotation `(-80, 0)`, and a terrain view at `(512, 300, 900)` with rotation `(-20, 0)`. Each view receives 256 warmup frames, draining the GPU after each frame so refinement can progress. Measurement starts only if a received feedback batch reports zero requests and uploads.

Each reported median comes from five batches of 120 frames. The benchmark drains the GPU every four frames to bound queued work. Results are wall-clock throughput, including CPU submission, transfers, and synchronization; they are not GPU timestamp measurements or windowed FPS. VSync, window-system presentation, and event processing are excluded. Linear-image hashes help detect differing rendered content on the same build environment and adapter; they are not portable reference hashes across GPUs or toolchains.

Set `VXRT_PERF_OVERLAY=1` to include egui construction, buffer updates, and overlay submission. In PowerShell:

```powershell
$env:VXRT_PERF_OVERLAY = '1'
cargo test --release --locked --test performance -- --ignored --nocapture
Remove-Item Env:VXRT_PERF_OVERLAY
```

The overlay benchmark reuses settled tree statistics. It does not measure periodic CPU tree-statistics scans or window input handling. Avoid running benchmarks concurrently. Repeat paired measurements because laptop clocks, thermals, driver scheduling, and background activity can shift results.

## Measured steady state

Measurements on 2026-11-10 used an NVIDIA GeForce RTX 4050 Laptop GPU, Vulkan backend, NVIDIA driver 610.91. The static packed-SVO reference was commit `8348547`; it ran the same benchmark workload with the render return type adapted and streaming-only statistics omitted. Both versions used identical defaults, camera positions, resolution, and presentation target.

| Implementation | Elevated ms/frame | Terrain ms/frame |
| --- | ---: | ---: |
| Dynamic streaming, one request word per leaf (`1657864`) | 1.592 | 2.630 |
| Dynamic streaming, cached flags + bitset + reused readback | 1.113 | 1.944 |
| Static packed SVO, adjacent comparison run | 1.114 | 1.998 |
| Dynamic streaming with egui, separate run | 1.105 | 1.979 |

For the final dynamic/static pair, the linear-image hashes matched exactly: elevated `40b8227f24b31000`, terrain `9343a9849b79fdbc`. This supports equivalent rendering work in these views. The final difference is within ordinary run-to-run variation; it does not establish a universal speed advantage or guarantee parity at every viewpoint, resolution, backend, or tree size. The overlay measurements similarly do not imply zero overlay cost: CPU/GPU work can overlap, and the runs were separate.

## Implemented changes

Refinement requests use one bit per leaf rather than one word. This reduces request-buffer clearing, transfer volume, and CPU scan length by up to 32×. Atomic OR preserves independent requests sharing a word. A completed readback buffer is unmapped and reused until its size changes, avoiding repeated steady-state buffer allocation. Feedback polling is skipped when no batch exists.

Descriptors cache occupancy and deferred status alongside the leaf index. Traversal no longer fetches a separate material record simply to decide whether a leaf is occupied or needs splitting. The leaf pool remains available for substantial material payloads. Parent descriptors must be updated if those cached flags change; see [tree design](tree.md).

An intermediate run with bitsets but without cached flags measured 1.217 / 2.159 ms per frame; cached flags plus bitsets measured 1.159 / 2.021 before readback reuse. These are directional observations, not isolated GPU instruction timings. The largest observed improvement came with compact feedback; caching flags alone did not consistently improve throughput. Correctness smoke tests cover split feedback, pool growth and reuse, scatter uploads, accumulation stability, and agreement with exact terrain.

## Next investigations

1. Use GPU timestamps and CPU profiling to separate traversal, transfer, submission, and overlay costs. The current benchmark establishes end-to-end throughput but cannot uniquely attribute the remaining time.
2. Measure larger resident pools after camera exploration. Bitset scans still scale with leaf capacity even when every word is zero. If this becomes significant, compare sparse request lists or hierarchical bitsets, including append contention, overflow handling, and CPU deduplication.
3. Profile active loading separately from steady state. Increasing `splits_per_frame` trades fewer loading frames for larger CPU bursts. Incremental ancestor-only merging could replace the full-tree compact pass after successful batches; worker generation is worthwhile only if profiling shows CPU generation dominates.
4. Compare scatter uploads with coalesced range writes at different dirty-slot densities, and reuse upload staging if allocation overhead is measurable. Steady-state tests contain no node patches and cannot answer this question.
5. Measure spatial locality after repeated allocation and reuse. GPU branch layout may affect traversal cache behavior, but relocation would require updating references and preserving the feedback snapshot invariant. Avoid adding this complexity without evidence.

Keep rendering-equivalence and convergence checks alongside performance experiments. Disabling refinement indefinitely after an empty batch would require reliable invalidation on camera, LOD, resolution, and topology changes; the current implementation continues collecting feedback and does not rely on such a shortcut.
