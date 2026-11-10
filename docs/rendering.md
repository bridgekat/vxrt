# Rendering architecture and development priorities

## Source layout

The `core` module contains CPU algorithms and has no dependency on windowing or GPU APIs. `renderer` owns device resources, command ordering, transfers, and readback. The application handles input and presentation, while integration tests exercise both layers.

| File | Responsibility |
| --- | --- |
| `src/core/terrain.rs` | Procedural height function and immutable min/max pyramid |
| `src/core/node.rs` | Regions, local occupancy classification, split results, and leaf compatibility |
| `src/core/pool.rs` | Stable slots, freelists, doubling capacity, and feedback invalidation logs |
| `src/core/tree.rs` | Runtime topology, refinement, merging, validation, and dirty slots |
| `src/core/packed.rs` | Packed scene import validation and independent exact reference generation |
| `src/renderer.rs` | GPU pipelines, dispatch, accumulation, and scene replacement |
| `src/renderer/streaming.rs` | Async refinement feedback, per-frame counters, pool growth, and scatter uploads |
| `src/renderer/readback.rs` | Synchronous diagnostic readback and scene PNG export |
| `src/stats_overlay.rs` | Read-only egui diagnostics composited over presentation |
| `src/config.rs` | TOML settings and parameter validation |
| `src/app.rs` | Window, input, camera motion, and event loop |
| `src/main.rs` | CLI entry point and logging |
| `src/shaders.rs` | Embedded shader assembly |
| `shaders/main.wgsl` | Traversal, interactive shading, and path transport |
| `shaders/sampling.wgsl` | Sample sequences, distributions, PDFs, and MIS weights |
| `shaders/scatter.wgsl` | Apply indexed slot updates before traversal |
| `shaders/present.wgsl` | Fullscreen presentation and gamma encoding |
| `tests/{terrain,node,pool,tree,packed,config,shaders}.rs` | CPU algorithms, settings, and shader validation |
| `tests/smoke.rs` | GPU execution, convergence, reference-image comparisons, and overlay previews |
| `tests/support/sampling.rs`, `tests/support/sampling.wgsl` | GPU sampling diagnostics against analytic integrals |

`Tree` is the runtime representation. The packed scene format is only an input/reference encoding, not a second runtime tree implementation. It has a node-count header followed by tagged words: branch words point to eight contiguous packed children, and leaf words contain occupancy 0 or 1. Import validates pointers, depth, tags, and occupancy, ignores unreachable storage, and constructs the runtime pools. Exact merging always uses `Tree::compact` and the local compatibility rule in `node.rs`.

## Tree generation and streaming

The CPU owns a sparse voxel tree backed by separate branch and leaf pools. Interactive rays request refinement from baked height bounds; the CPU splits and merges nodes, then uploads changed slots through scatter writes. Generated detail remains resident, and path tracing freezes topology. Deferred leaves render as coarse solid previews.

See [sparse voxel tree design](tree.md) for data layouts, local operations, allocation, GPU-visible snapshot invariants, command ordering, feedback counters, and residency policy.

## Interactive rendering

The camera uses inverse perspective and rotation matrices to construct rays. Movement is always free flight and advances at 30 Hz with interpolation between updates. The initial camera position is centered above the world.

Interactive frames execute two compute passes with 8×8 workgroups. The first casts a coarse grid of beams at four-pixel spacing. The second uses neighboring beam distances to skip empty space before tracing full-resolution rays. The beam grid includes an extra row and column beyond its rounded-up viewport coverage. Separate parameter buffers and compute passes provide distinct dispatch parameters and ordered storage access.

Traversal uses a 20-entry stack and a configurable `march_budget` (default 1024, allowed range 1–65536) per ray. The setting applies to beam, interactive, path-continuation, and shadow rays. Box intersections and test-point offsets determine movement between tree nodes. Interactive shading applies a directional diffuse factor to neutral surfaces with albedo 0.8 using geometric voxel-face normals, with sky color for misses and traversal-cost fading. Surfaces have no grid overlay. The profiler view displays per-pixel traversal iteration cost. Each interactive frame overwrites the color buffer.

## Path tracing

Path tracing freezes the camera and pauses CPU splits and merging, including the G control. GPU rays do not request refinement while accumulating. Completed feedback can be discarded without applying it. The resident SVO and camera-dependent LOD remain stable; missing detail is rendered as coarse solid geometry even for shadow and continuation rays. Returning to interactive mode resumes streaming.

Path rays trace from the camera without beam skipping. Camera jitter samples uniformly within each pixel's footprint. Transport uses normalized geometric voxel-face normals and a neutral Lambertian BRDF `0.8 / pi` (constant RGB albedo 0.8).

At every surface:

1. Sample one direction uniformly in solid angle over the solar disk. For half-angle `a = 0.05` radians, its PDF is `1 / (2*pi*(1-cos(a)))` inside the cone and zero outside. A shadow ray tests visibility. The contribution is `throughput * (0.8/pi) * sun_radiance * max(dot(normal, direction), 0) / light_pdf` multiplied by its MIS weight.
2. Sample a cosine-weighted hemisphere around the surface normal. Its PDF is `max(dot(normal, direction), 0) / pi`, so the continuation throughput is multiplied by 0.8 before roulette.
3. Combine light and scattering samples with the power heuristic, `pdf_a² / (pdf_a² + pdf_b²)`. A continuation ray escaping toward the solar disk receives the complementary scattering weight. Outside the solar disk, the light PDF is zero and the escape contribution has weight one. Camera rays see the environment without MIS weighting.
4. After three surface bounces, apply Russian roulette. Survival probability is the largest throughput component clamped to `[0.05, 0.95]`; surviving paths divide throughput by that probability. Terminated paths return only radiance already collected. There is no fixed bounce cutoff and no sky contribution without an escaping ray.

Shadow and continuation rays start at the surface boundary with a test point in the adjacent empty cell and an independent traversal stack. A traversal-budget failure conservatively contributes no further light instead of treating failure as visible sky.

The estimator follows the standard combination of light sampling, cosine-weighted scattering, and multiple importance sampling. [PBRT: A Better Path Tracer](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer) The hemisphere and cone PDFs are expressed with respect to solid angle. [PBRT: Sampling Multidimensional Functions](https://pbr-book.org/4ed/Sampling_Algorithms/Sampling_Multidimensional_Functions)

## Lighting controls and sun visibility

The environment uses an approximate sea-level, single-scattering atmosphere: wavelength-dependent Rayleigh scattering and forward-peaked Henyey-Greenstein Mie scattering. Exponential density scale heights determine the optical depth, with a bounded air-mass approximation and Beer-Lambert attenuation producing a blue zenith, pale horizon, and solar haze. The model uses a fixed exposure in linear radiance, extends horizon density below the horizon, and omits altitude changes, ground occlusion, and multiple scattering. Both interactive rendering and path tracing use this same environment.

`sky_intensity` and `sun_intensity` are independent nonnegative linear-radiance multipliers, defaulting to 1.0 and 256.0 respectively. Sky intensity affects the environment outside the solar disk. Sun intensity affects the disk seen by both camera/scattering rays and explicit light samples. Changing intensity does not change the cone PDF or MIS weights.

The sun disk's half-angle is 0.05 radians, so its solid angle is approximately 0.00785 steradians. The default sun radiance is 256.0, providing visible direct illumination despite the small solid angle. For an unoccluded horizontal Lambertian plane with normal `n` and sun axis `s`, the reflected solar radiance is `0.8 * sun_radiance * dot(n,s) * sin(0.05)^2` when the whole disk is above the horizon.

To view sunlight against a black sky, use `sky_intensity = 0.0` and keep `sun_intensity` at its default of `256.0`.

## Sample sequences and spatial coherence

Samples are stateless integer-hash functions of seed, sample index, dimension, and a pixel key. Each value lies strictly inside `(0,1)`. Dimensions 0–1 provide camera jitter. Every bounce has five distinct dimensions: two for the light sample, two for the scattering sample, and one for roulette. Time and floating-point bit patterns do not index the sequence.

`coherent_sampling = true` is the default. Bounce dimensions use a shared pixel key, so neighboring pixels receive the same random values for the same bounce and sample number. Camera jitter still uses each pixel's own key. Shared bounce samples can improve ray coherence and traversal efficiency. Spatial correlation does not itself bias the per-pixel estimator; temporal samples still vary and the PDFs and weights are unchanged.

Set `coherent_sampling = false` to key bounce dimensions by pixel as well. This changes spatial noise and may help spatial averaging or denoising, at a possible cost to traversal coherence. Neither choice is universally faster at a given image quality. Compare equal-time error and GPU timings on representative scenes before choosing a mode. The hash sampler is pseudorandom, not a low-discrepancy sequence.

## Accumulation and presentation

Path samples are averaged in linear light. Mode switches, camera changes, render-resolution changes, and tree replacement or compaction reset accumulation. At `2^24` accumulated samples, the running average restarts to keep its count exactly representable in shader floating-point arithmetic.

Color and beam data reside in storage buffers. The presentation shader bilinearly samples the color buffer and applies gamma 2.2 encoding through a non-sRGB target view. Screenshot export uses the same gamma curve. Display/export clamps out-of-range values; transport tests check that linear radiance is finite and nonnegative before display conversion.

## Tree statistics

The egui overlay displays branch and leaf counts versus pool capacity, free slots, exact empty/solid and deferred leaf counts, live versus reserved GPU pool bytes, baked height-bound bytes, and pending feedback. It also shows FPS or accumulated samples, camera position, and whether refinement is paused. Tree counts are sampled on the CPU every 250 ms; they do not trigger GPU readback. Activity counters update every render frame: GPU-to-CPU requests count unique marked leaf slots in the batch received during that frame, while CPU-to-GPU updates count unique live branch and leaf slots patched that frame. The latter excludes frame uniforms, unchanged pool data copied during growth, and freed unreachable slots. Feedback carries its source frame number because it usually arrives later than the rays that generated it. No received batch is explicitly distinguished from a received batch containing zero requests.

Additional counters report successful splits, unprocessed requests beyond the split budget, pool-capacity failures, and stale requests. A stationary view should settle to zero requests and zero node patches once its visible LOD demand has been serviced, provided pool capacity is sufficient. A finite march budget can still truncate rays; zero requests is not proof of complete geometric coverage. The total deferred-leaf count can remain nonzero because unseen or sufficiently coarse regions do not need refinement. GPU memory numbers cover branch and leaf pools only, excluding feedback buffers, upload staging, framebuffers, and CPU allocator metadata. Height-bound storage reports pyramid element bytes.

C validates the tree, compares resident GPU slots, and logs the current statistics. The overlay is composited after scene presentation and is not stored in path accumulation or scene PNG exports. The GPU smoke suite writes a separate overlay preview to `target/smoke/stats-overlay.png`.

## Validation and limits

`cargo test --locked` runs integration tests for terrain occupancy and height cutoffs, malformed octree rejection, compaction preservation and idempotence, configuration constraints, and the render and diagnostic shaders. The GPU smoke test is ignored by default because it requires a graphics adapter. `cargo test --release --locked --test smoke -- --ignored --nocapture` also checks:

- Hemisphere support, normalized directions, cone solid-angle moments, sample range, dimension decorrelation, and seed/index/pixel separation.
- Shared bounce values with per-pixel camera jitter in coherent mode, and distinct bounce values in decorrelated mode.
- MIS estimates against analytic integrals for an aligned sun, a tilted sun, a sun centered on the horizon, and a constant environment. Distribution checks use 65,536 samples; integral tolerances are 2%.
- Finite nonnegative transport, exact sample replay on the same adapter, numerical accumulation, frozen geometry, camera/resize resets, and compaction in both sampling modes.
- GPU feedback-driven refinement, pool doubling, scattered updates checked against CPU slots, retention after moving the camera far away, and egui overlay rendering.
- Per-frame request/update accounting, convergence to zero work for fixed views (including distant visible terrain), and agreement with exact terrain rendering.
- Configurable march budgets through the complete GPU pipeline: a deliberately truncated empty-world ray and its successful completion with a larger budget.
- A fully enclosed non-emissive room receiving zero environment light, and an open room receiving positive light.

The renderer approximates geometry through camera-dependent LOD and deferred solid previews, including for secondary rays. Frozen accumulation converges to that resident approximation, not necessarily the fully refined terrain. Traversal-budget failures can darken an image; the transport equations do not remove that geometric/traversal approximation. Floating-point backend differences can affect results. GPU diagnostics are correctness checks, not a cross-backend image-equivalence or performance benchmark. Collision detection is not implemented.

## Handling traversal limits

Set `march_budget` in `config.toml` and restart to adjust the limit. Larger budgets allow more work per ray, at the cost of longer frames; profiler values are normalized by the configured budget. The setting limits traversal iterations, not path bounces or stack depth.

The current path tracer returns radiance already collected when a continuation ray exceeds the traversal budget; the unknown remainder contributes zero. Failure on the camera ray therefore produces black. A shadow-ray failure likewise omits that direct-light contribution. This is conservative about visibility but introduces dark bias.

A more robust design would distinguish `hit`, `miss`, and `pending` results. Pending rays could resume from stored traversal/path state in later dispatches until visibility is resolved. This turns the per-dispatch step limit into a scheduling limit instead of a transport cutoff. The saved state must cover shadow rays as well as continuation rays, including pending direct-light contributions, throughput, sample dimensions, and MIS data. Queued path state is a standard GPU rendering organization. [PBRT: Mapping Path Tracing to the GPU](https://www.pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Mapping_Path_Tracing_to_the_GPU)

A simpler first step would retry only failed samples with a larger step budget, using the same sample index and dimensions. Do not accumulate the failed partial result and then add the retried result again. Unfinished samples need to remain pending; track completed-sample counts per pixel, or finish the entire sample wave before advancing a shared count. Retrying still needs bounded dispatch times and a policy for persistently expensive rays.

While a sample is pending, presentation can retain the last completed estimate or show a separate diagnostic overlay. That displayed value must not be counted as another sample. Permanently discarding failures instead of retrying can bias the result toward short/easy paths. Substituting the sky can leak light through occluders; coarse geometry or cached lighting is a deliberate preview approximation rather than a correctness fix.

## Development priorities

1. **Measure image quality and cost together.** Fix scene, camera, seed, exposure, and sampling mode. Compare equal-time error against a high-sample reference; measure beam traversal, path traversal, and presentation independently. Include traversal-budget failures and coarse-node counts in diagnostics.
2. **Benchmark coherent versus decorrelated sampling.** Measure both GPU time and spatial noise, including any denoiser. Evaluate scrambled low-discrepancy sequences as an additional option while retaining deliberate spatial coherence where useful.
3. **Evaluate LOD accuracy.** Compare coarse traversal against full-detail geometry, especially for shadow and indirect paths. Measure the cost of retaining finer detail for secondary rays.
4. **Validate and optimize traversal.** Compare traversal with a simple oracle at grazing angles, corners, and beam boundaries. Profile memory locality and compaction before changing algorithms.
5. **Extend transport deliberately.** Consider environment importance sampling, more material models, shading-normal corrections, adaptive sampling, and denoising with variance tracking. Keep analytic and enclosed-scene regression tests for every estimator change.

## Evaluating future eviction

A candidate policy is budget-triggered approximate LRU over subtrees: retain detail while there is space, then reclaim the least recently traversed eligible regions. Track actual traversal use, including empty-space traversal, rather than split-request recency; already-refined regions no longer generate split requests. Preserve required ancestors, add a grace period to avoid repeated eviction and regeneration, and keep eviction disabled during accumulation. A per-frame touched flag can avoid maintaining a strict ordered LRU list on the GPU. This is a proposed policy, not implemented behavior.

Usage tracking and synchronization are established requirements for GPU residency management; the subtree selection policy would still need renderer-specific measurements. [Microsoft residency sample](https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Residency/readme.md)

## Evaluating MCMC sampling

Markov chain Monte Carlo (MCMC) for rendering commonly takes the form of Metropolis Light Transport (MLT). It mutates light-carrying paths and concentrates work near high-contribution paths. This can help when useful paths are rare, including indirect illumination through small openings, difficult visibility, or glossy transport. [Metropolis Light Transport](https://graphics.stanford.edu/papers/metro/)

MLT is not the recommended default for this diffuse terrain, sky, and sun workload. It requires chain initialization, normalization, mutation tuning, and management of correlated samples. Camera changes invalidate accumulated results and require reassessing chain state. [PBRT: Metropolis Light Transport](https://www.pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/Metropolis_Light_Transport)

Shared random dimensions across pixels are a separate design choice from MCMC: they do not create a Markov chain over light paths. If difficult indirect-light scenes become a target, evaluate primary sample space MLT (PSSMLT) as an offline option against the MIS path tracer. Compare equal-time image error, acceptance rate, effective sample size, and coverage of bright paths using many independent chains.
