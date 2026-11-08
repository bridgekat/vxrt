# Rendering architecture and development priorities

## Source layout

| File | Responsibility |
| --- | --- |
| `src/main.rs` | Command-line entry points and logging |
| `src/app.rs` | Window, input, camera motion, surface presentation, and event loop |
| `src/config.rs` | TOML configuration and parameter validation |
| `src/world.rs` | Static terrain, packed octree construction, and dynamic noise maps |
| `src/renderer.rs` | GPU resources, dispatch, accumulation state, readback, and screenshots |
| `src/shaders.rs` | Embedded GPU program assembly |
| `src/smoke.rs` | GPU rendering checks, controlled scenes, and preview generation |
| `src/sampling_checks.rs` / `tests/sampling.wgsl` | GPU sampling diagnostics against analytic integrals |
| `shaders/sampling.wgsl` | Sample dimensions, direction distributions, PDFs, and MIS weights |
| `shaders/main.wgsl` | Terrain generation, traversal, interactive shading, and path tracing |
| `shaders/present.wgsl` | Fullscreen presentation and gamma encoding |

## Terrain and octree representation

A world is a cube with side length `2^world_levels`. The tree buffer begins with a 32-bit allocation count followed by 32-bit node words. Low bits encode node state; upper bits hold either a child pointer or a leaf material:

| Node word | Meaning |
| --- | --- |
| `0` | Ungenerated node |
| `1` | Locked or unresolved coarse node |
| `(child_pointer << 2) \| 1` | Internal node with eight contiguous children |
| `(material << 2) \| 3` | Leaf; material zero is empty and nonzero is solid |

Child indices encode X, Y, and Z in bits 0, 1, and 2 respectively.

Static terrain uses five-octave interpolated noise, a composed height function with a +64 offset, and a maximum-height cutoff. Integer hash arithmetic explicitly wraps. The CPU builds the tree and merges identical leaf children bottom-up before upload.

Interactive dynamic terrain uses seeded noise textures with min/max mipmaps to bound heights. Nodes are generated lazily with atomic operations and view-dependent level of detail (LOD). A bounded compare-and-swap loop allocates children without exceeding capacity. Free slots are zeroed before dispatch, including after compaction, so publishing a parent pointer does not depend on cross-invocation visibility of child-initialization writes. Allocation exhaustion leaves coarse opaque placeholders. Tree checking and compaction synchronize with the GPU before reading node data.

## Interactive rendering

The camera uses inverse perspective and rotation matrices to construct rays. Movement advances at 30 Hz with interpolation between updates. The initial camera position is centered above the world.

Interactive frames execute two compute passes with 8×8 workgroups. The first casts a coarse grid of beams at four-pixel spacing. The second uses neighboring beam distances to skip empty space before tracing full-resolution rays. The beam grid includes an extra row and column beyond its rounded-up viewport coverage. Separate parameter buffers and compute passes provide distinct dispatch parameters and ordered storage access.

Traversal uses a 20-entry stack and a 256-iteration budget. Box intersections and test-point offsets determine movement between tree nodes. Interactive shading combines a directional sun, sky colors, a normal-dependent palette, and voxel edge outlines. Dynamic terrain uses height-gradient shading normals. The profiler view displays per-pixel traversal iteration cost. Each interactive frame overwrites the color buffer.

## Path tracing

Path tracing freezes the camera and dynamic tree. Ungenerated and unresolved nodes are treated as opaque coarse cells; no nodes are allocated while accumulating. Explore the scene in interactive mode to generate detail before pressing **P**. Returning to interactive mode resumes generation. This keeps geometry and camera-dependent LOD stable throughout an accumulation sequence.

Path rays trace from the camera without beam skipping. Camera jitter samples uniformly within each pixel's footprint. Transport uses normalized geometric voxel-face normals and a Lambertian BRDF `albedo / pi`. Terrain-gradient normals remain an interactive shading feature; using face normals for transport avoids sampling through the geometric surface.

At every surface:

1. Sample one direction uniformly in solid angle over the solar disk. For half-angle `a = 0.05` radians, its PDF is `1 / (2*pi*(1-cos(a)))` inside the cone and zero outside. A shadow ray tests visibility. The contribution is `throughput * (albedo/pi) * sun_radiance * max(dot(normal, direction), 0) / light_pdf` multiplied by its MIS weight.
2. Sample a cosine-weighted hemisphere around the surface normal. Its PDF is `max(dot(normal, direction), 0) / pi`, so the continuation throughput simplifies to multiplication by `albedo`.
3. Combine light and scattering samples with the power heuristic, `pdf_a² / (pdf_a² + pdf_b²)`. A continuation ray escaping toward the solar disk receives the complementary scattering weight. Outside the solar disk, the light PDF is zero and the escape contribution has weight one. Camera rays see the environment without MIS weighting.
4. After three surface bounces, apply Russian roulette. Survival probability is the largest throughput component clamped to `[0.05, 0.95]`; surviving paths divide throughput by that probability. Terminated paths return only radiance already collected. There is no fixed bounce cutoff and no sky contribution without an escaping ray.

Shadow and continuation rays start at the surface boundary with a test point in the adjacent empty cell and an independent traversal stack. A traversal-budget failure conservatively contributes no further light instead of treating failure as visible sky.

The estimator follows the standard combination of light sampling, cosine-weighted scattering, and multiple importance sampling. [PBRT: A Better Path Tracer](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer) The hemisphere and cone PDFs are expressed with respect to solid angle. [PBRT: Sampling Multidimensional Functions](https://pbr-book.org/4ed/Sampling_Algorithms/Sampling_Multidimensional_Functions)

## Sample sequences and spatial coherence

Samples are stateless integer-hash functions of seed, sample index, dimension, and a pixel key. Each value lies strictly inside `(0,1)`. Dimensions 0–1 provide camera jitter. Every bounce has five distinct dimensions: two for the light sample, two for the scattering sample, and one for roulette. Time and floating-point bit patterns do not index the sequence.

`coherent_sampling = true` is the default. Bounce dimensions use a shared pixel key, so neighboring pixels receive the same random values for the same bounce and sample number. Camera jitter still uses each pixel's own key. Shared bounce samples can improve ray coherence and traversal efficiency. Spatial correlation does not itself bias the per-pixel estimator; temporal samples still vary and the PDFs and weights are unchanged.

Set `coherent_sampling = false` to key bounce dimensions by pixel as well. This changes spatial noise and may help spatial averaging or denoising, at a possible cost to traversal coherence. Neither choice is universally faster at a given image quality. Compare equal-time error and GPU timings on representative scenes before choosing a mode. The hash sampler is pseudorandom, not a low-discrepancy sequence.

## Accumulation and presentation

Path samples are averaged in linear light. Mode switches, camera changes, render-resolution changes, and tree replacement or compaction reset accumulation. At `2^24` accumulated samples, the running average restarts to keep its count exactly representable in shader floating-point arithmetic.

Color and beam data reside in storage buffers. The presentation shader bilinearly samples the color buffer and applies gamma 2.2 encoding through a non-sRGB target view. Screenshot export uses the same gamma curve. Display/export clamps out-of-range values; transport tests check that linear radiance is finite and nonnegative before display conversion.

## Validation and limits

`cargo test --locked` checks CPU terrain and bounds, configuration constraints, and the render and diagnostic shaders. `cargo run --release --locked -- --smoke-test` also checks:

- Hemisphere support, normalized directions, cone solid-angle moments, sample range, dimension decorrelation, and seed/index/pixel separation.
- Shared bounce values with per-pixel camera jitter in coherent mode, and distinct bounce values in decorrelated mode.
- MIS estimates against analytic integrals for an aligned sun, a tilted sun, a sun centered on the horizon, and a constant environment. Distribution checks use 65,536 samples; integral tolerances are 2%.
- Finite nonnegative transport, exact sample replay on the same adapter, numerical accumulation, frozen dynamic trees, camera/resize resets, compaction, and allocation exhaustion in both sampling modes.
- A fully enclosed non-emissive room receiving zero environment light, and an open room receiving positive light.

The renderer still approximates geometry through LOD and coarse placeholders. Freezing a dynamic tree can leave secondary-ray regions under-detailed. Traversal-budget failures can darken an image; the transport equations do not remove that geometric/traversal approximation. Floating-point backend differences and interactive generation order can affect results. GPU diagnostics are correctness checks, not a cross-backend image-equivalence or performance benchmark. Collision detection is not implemented.

## Development priorities

1. **Measure image quality and cost together.** Fix scene, camera, seed, exposure, and sampling mode. Compare equal-time error against a high-sample reference; measure beam traversal, path traversal, and presentation independently. Include traversal-budget failures and coarse-node counts in diagnostics.
2. **Benchmark coherent versus decorrelated sampling.** Measure both GPU time and spatial noise, including any denoiser. Evaluate scrambled low-discrepancy sequences as an additional option while retaining deliberate spatial coherence where useful.
3. **Improve frozen-scene coverage.** Add a generation warm-up or explicit refinement step covering shadow and indirect paths before accumulation. Provide visible feedback when the frozen tree contains unresolved cells.
4. **Validate and optimize traversal.** Compare traversal with a simple oracle at grazing angles, corners, and beam boundaries. Profile memory locality, allocation, and compaction before changing algorithms.
5. **Extend transport deliberately.** Consider environment importance sampling, more material models, shading-normal corrections, adaptive sampling, and denoising with variance tracking. Keep analytic and enclosed-scene regression tests for every estimator change.

## Evaluating MCMC sampling

Markov chain Monte Carlo (MCMC) for rendering commonly takes the form of Metropolis Light Transport (MLT). It mutates light-carrying paths and concentrates work near high-contribution paths. This can help when useful paths are rare, including indirect illumination through small openings, difficult visibility, or glossy transport. [Metropolis Light Transport](https://graphics.stanford.edu/papers/metro/)

MLT is not the recommended default for this diffuse terrain, sky, and sun workload. It requires chain initialization, normalization, mutation tuning, and management of correlated samples. Camera changes invalidate accumulated results and require reassessing chain state. [PBRT: Metropolis Light Transport](https://www.pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/Metropolis_Light_Transport)

Shared random dimensions across pixels are a separate design choice from MCMC: they do not create a Markov chain over light paths. If difficult indirect-light scenes become a target, evaluate primary sample space MLT (PSSMLT) as an offline option against the MIS path tracer. Compare equal-time image error, acceptance rate, effective sample size, and coverage of bright paths using many independent chains.
