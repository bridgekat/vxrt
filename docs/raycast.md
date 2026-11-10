# Ray-casting protocol

`shaders/main.wgsl` traverses the CPU-owned resident SVO. `src/renderer.rs` supplies frame parameters and orders GPU passes. See [tree design](tree.md) for node encoding and refinement, and [sampling](sampling.md) for path transport and accumulation.

## Interactive rendering

The camera uses inverse perspective and rotation matrices to construct rays. Movement is always free flight and advances at 30 Hz with interpolation between updates. The initial camera position is centered above the world.

Interactive frames execute two compute passes with 8×8 workgroups. The first casts a coarse grid of beams at four-pixel spacing. The second uses neighboring beam distances to skip empty space before tracing full-resolution rays. The beam grid includes an extra row and column beyond its rounded-up viewport coverage. Separate parameter buffers and compute passes provide distinct dispatch parameters and ordered storage access.

Traversal uses a 20-entry stack and a configurable `march_budget` (default 1024, allowed range 1–65536) per ray. The setting applies to beam, interactive, path-continuation, and shadow rays. Box intersections and test-point offsets determine movement between tree nodes. Interactive shading applies a directional diffuse factor to neutral surfaces with albedo 0.8 using geometric voxel-face normals, with sky color for misses and traversal-cost fading. Surfaces have no grid overlay. The profiler view displays per-pixel traversal iteration cost. Each interactive frame overwrites the color buffer.

## Traversal state and results

`start_ray` initializes the test point and last intersection. `cast_ray` intersects the world cube if the origin is outside it, then descends from the root or resumes from the per-invocation ancestor stack. Each stack entry records a region origin and branch descriptor; its depth determines region size. Child selection uses the test point relative to the region midpoint, with X/Y/Z in child-index bits 0/1/2.

Runtime descriptors cache leaf occupancy and deferred status. Exact empty leaves advance the ray to the next cell boundary using `inner_intersect`; `last.pos` remains the geometric intersection while `test_point` is offset into the next region to choose the next traversal cell. The algorithm climbs the stack when this point exits a region. Exact solid leaves, deferred solid previews, and branch LOD stops terminate traversal as occupied geometry.

`cast_ray` returns a negative value for a miss, a value in `[0, 1)` for a hit (iterations divided by `march_budget`), and `1` for traversal-budget or stack exhaustion. Callers must distinguish exhaustion from a miss. Interactive profiling visualizes traversal cost. Path tracing contributes no further light for exhaustion; a shadow-ray failure is not treated as visible sky.

LOD is camera-dependent even for secondary rays. The test compares node size with projected pixel footprint using `lod_quality`; beams apply an additional spacing factor. An LOD stop is a solid preview and does not change the resident tree. Higher quality retains finer detail. There is no CPU distance cutoff or distance eviction.

Shadow and continuation rays restart with an empty traversal stack at the surface boundary, using a test point in the adjacent empty cell. Normals come from voxel faces. Collision detection is not implemented.

## Refinement output contract

Interactive primary rays and coarse beams each have a distinct request slot. A deferred leaf is solid, so a ray can encounter at most one requested leaf before returning. The fixed-size list is therefore one entry per invocation (`k = 1`). Zero means no request; a nonzero word stores the leaf index plus one. There are no global atomics or cross-ray deduplication during traversal.

The collection flag in `Params.scene.w` enables writes only when the CPU can capture a feedback batch. Each in-bounds invocation initializes its slot before tracing. Primary pixels occupy the first `width * height` slots; coarse beam slots follow. Rounded-up dispatch lanes outside the actual grid return without touching memory. A tree-buffer resize does not resize requests; changing render resolution does.

Accumulation disables collection and skips the coarse pass. Interactive frames with a pending readback still render, but skip request writes and merge passes. The current one-request bound relies on opaque deferred geometry and interactive-only collection. Transparent geometry, multi-hit traversal, or requesting from secondary rays would require revisiting this contract.

After traversal, hierarchical compute passes concatenate bounded lists into a single packet. That reduction and asynchronous readback belong to the [tree feedback protocol](tree.md#sparse-request-collection).

## Dispatch and presentation

Pool growth copies and scatter updates precede traversal. Separate beam and primary compute passes provide storage visibility; each uses its own parameter buffer. Presentation reads the linear color buffer, bilinearly samples it, and gamma-encodes through a non-sRGB view. The read-only egui overlay is composited afterward and is excluded from accumulation and scene PNG exports.

A frame's request merge and readback copy are ordered after traversal. The normal feedback path uses asynchronous mapping and nonblocking device polling. Diagnostic image export and tree verification use synchronous readback instead.
## Handling traversal limits

Set `march_budget` in `config.toml` and restart to adjust the limit. Larger budgets allow more work per ray, at the cost of longer frames; profiler values are normalized by the configured budget. The setting limits traversal iterations, not path bounces or stack depth.

The current path tracer returns radiance already collected when a continuation ray exceeds the traversal budget; the unknown remainder contributes zero. Failure on the camera ray therefore produces black. A shadow-ray failure likewise omits that direct-light contribution. This is conservative about visibility but introduces dark bias.

A more robust design would distinguish `hit`, `miss`, and `pending` results. Pending rays could resume from stored traversal/path state in later dispatches until visibility is resolved. This turns the per-dispatch step limit into a scheduling limit instead of a transport cutoff. The saved state must cover shadow rays as well as continuation rays, including pending direct-light contributions, throughput, sample dimensions, and MIS data. Queued path state is a standard GPU rendering organization. [PBRT: Mapping Path Tracing to the GPU](https://www.pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Mapping_Path_Tracing_to_the_GPU)

A simpler first step would retry only failed samples with a larger step budget, using the same sample index and dimensions. Do not accumulate the failed partial result and then add the retried result again. Unfinished samples need to remain pending; track completed-sample counts per pixel, or finish the entire sample wave before advancing a shared count. Retrying still needs bounded dispatch times and a policy for persistently expensive rays.

While a sample is pending, presentation can retain the last completed estimate or show a separate diagnostic overlay. That displayed value must not be counted as another sample. Permanently discarding failures instead of retrying can bias the result toward short/easy paths. Substituting the sky can leak light through occluders; coarse geometry or cached lighting is a deliberate preview approximation rather than a correctness fix.

## Validation and profiling

`cargo test --locked` validates the WGSL programs. `cargo test --release --locked --test smoke -- --ignored --nocapture` exercises actual GPU traversal, odd viewport sizes, resize and 1×1 targets, march-budget failures, stable-view convergence, and comparisons with exact terrain. These checks are not a guarantee of identical floating-point results across adapters.

For steady-state throughput, run `cargo test --release --locked --test performance -- --ignored --nocapture` on an otherwise idle GPU. It uses 1280×720 interactive rendering with presentation, 256 warmup frames per view, and five batches of 120 measured frames. GPU work is drained every four frames to bound queue depth. Reported times include submission and synchronization, not just shader execution; window/VSync and event handling are excluded. Set `VXRT_PERF_OVERLAY=1` to include overlay rendering with cached settled tree statistics. The benchmark prints linear-image hashes and requires zero request/update activity before timing.

Measure traversal, request merging, transfers, and presentation separately with GPU timestamps before attributing an end-to-end difference to any one pass. Repeat paired runs to account for clock and thermal variation. For traversal changes, compare grazing angles, corners, beam boundaries, and secondary-ray LOD against a simpler geometric oracle. Memory locality and finer shadow-ray LOD are useful next experiments.
