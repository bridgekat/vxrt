# Rendering architecture and development priorities

## Source layout

| File | Responsibility |
| --- | --- |
| `src/main.rs` | Command-line entry points and logging |
| `src/lib.rs` | Shared configuration, terrain, shaders, and renderer API |
| `tests/config.rs`, `tests/world.rs`, `tests/octree.rs`, `tests/shaders.rs` | Configuration, terrain, octree, and shader integration checks |
| `src/app.rs` | Window, input, camera motion, surface presentation, and event loop |
| `src/config.rs` | TOML configuration and parameter validation |
| `src/world.rs` | CPU terrain generation and packed octree construction |
| `src/renderer.rs` | GPU resources, dispatch, accumulation state, and tree upload |
| `src/renderer/readback.rs` | GPU readback and screenshots |
| `src/octree.rs` | CPU validation and compaction of packed trees |
| `src/shaders.rs` | Embedded GPU program assembly |
| `tests/smoke.rs` | GPU rendering checks, controlled scenes, and preview generation |
| `tests/support/sampling.rs` / `tests/support/sampling.wgsl` | GPU sampling diagnostics against analytic integrals |
| `shaders/sampling.wgsl` | Sample dimensions, direction distributions, PDFs, and MIS weights |
| `shaders/main.wgsl` | Traversal, interactive shading, and path tracing |
| `shaders/present.wgsl` | Fullscreen presentation and gamma encoding |

## Terrain and octree representation

A world is a cube with side length `2^world_levels`. The tree buffer begins with a 32-bit node count followed by 32-bit node words. Low bits encode node state; upper bits hold either a child pointer or a leaf material:

| Node word | Meaning |
| --- | --- |
| `(child_pointer << 2) \| 1` | Internal node with eight contiguous children |
| `(material << 2) \| 3` | Leaf; material zero is empty and nonzero is solid |

Child indices encode X, Y, and Z in bits 0, 1, and 2 respectively.

Terrain uses five-octave interpolated noise, a composed height function with a +64 offset, and an exclusive maximum-height cutoff: voxels with `y >= max_height` are empty, including at leaf resolution. Integer hash arithmetic explicitly wraps. The CPU builds the tree and merges identical leaf children bottom-up before upload.

The GPU reads the completed tree without modifying it. View-dependent level of detail (LOD) can stop traversal at an internal node and treat that region as a coarse opaque cell. Tree checking and compaction synchronize with the GPU before reading node data. CPU validation checks the header, node tags, pointer bounds, repeated references, and depth. Compaction removes unreachable storage and merges identical leaf children. Replacement trees are validated before upload and must fit the existing GPU buffer; uploading resets accumulation.

## Interactive rendering

The camera uses inverse perspective and rotation matrices to construct rays. Movement is always free flight and advances at 30 Hz with interpolation between updates. The initial camera position is centered above the world.

Interactive frames execute two compute passes with 8×8 workgroups. The first casts a coarse grid of beams at four-pixel spacing. The second uses neighboring beam distances to skip empty space before tracing full-resolution rays. The beam grid includes an extra row and column beyond its rounded-up viewport coverage. Separate parameter buffers and compute passes provide distinct dispatch parameters and ordered storage access.

Traversal uses a 20-entry stack and a configurable `march_budget` (default 1024, allowed range 1–65536) per ray. The setting applies to beam, interactive, path-continuation, and shadow rays. Box intersections and test-point offsets determine movement between tree nodes. Interactive shading applies a directional diffuse factor to neutral surfaces with albedo 0.8 using geometric voxel-face normals, with sky color for misses and traversal-cost fading. Surfaces have no grid overlay. The profiler view displays per-pixel traversal iteration cost. Each interactive frame overwrites the color buffer.

## Path tracing

Path tracing freezes the camera so geometry and camera-dependent LOD stay stable throughout an accumulation sequence. The terrain is fully constructed before either rendering mode begins.

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

## Validation and limits

`cargo test --locked` runs integration tests for terrain occupancy and height cutoffs, malformed octree rejection, compaction preservation and idempotence, configuration constraints, and the render and diagnostic shaders. The GPU smoke test is ignored by default because it requires a graphics adapter. `cargo test --release --locked --test smoke -- --ignored --nocapture` also checks:

- Hemisphere support, normalized directions, cone solid-angle moments, sample range, dimension decorrelation, and seed/index/pixel separation.
- Shared bounce values with per-pixel camera jitter in coherent mode, and distinct bounce values in decorrelated mode.
- MIS estimates against analytic integrals for an aligned sun, a tilted sun, a sun centered on the horizon, and a constant environment. Distribution checks use 65,536 samples; integral tolerances are 2%.
- Finite nonnegative transport, exact sample replay on the same adapter, numerical accumulation, read-only geometry, camera/resize resets, and compaction in both sampling modes.
- Configurable march budgets through the complete GPU pipeline: a deliberately truncated empty-world ray and its successful completion with a larger budget.
- A fully enclosed non-emissive room receiving zero environment light, and an open room receiving positive light.

The renderer still approximates geometry through camera-dependent LOD, including for secondary rays. Traversal-budget failures can darken an image; the transport equations do not remove that geometric/traversal approximation. Floating-point backend differences can affect results. GPU diagnostics are correctness checks, not a cross-backend image-equivalence or performance benchmark. Collision detection is not implemented.

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

## Evaluating MCMC sampling

Markov chain Monte Carlo (MCMC) for rendering commonly takes the form of Metropolis Light Transport (MLT). It mutates light-carrying paths and concentrates work near high-contribution paths. This can help when useful paths are rare, including indirect illumination through small openings, difficult visibility, or glossy transport. [Metropolis Light Transport](https://graphics.stanford.edu/papers/metro/)

MLT is not the recommended default for this diffuse terrain, sky, and sun workload. It requires chain initialization, normalization, mutation tuning, and management of correlated samples. Camera changes invalidate accumulated results and require reassessing chain state. [PBRT: Metropolis Light Transport](https://www.pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/Metropolis_Light_Transport)

Shared random dimensions across pixels are a separate design choice from MCMC: they do not create a Markov chain over light paths. If difficult indirect-light scenes become a target, evaluate primary sample space MLT (PSSMLT) as an offline option against the MIS path tracer. Compare equal-time image error, acceptance rate, effective sample size, and coverage of bright paths using many independent chains.
