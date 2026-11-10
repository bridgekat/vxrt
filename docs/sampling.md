# Sampling and light transport

Sampling and transport are implemented in `shaders/sampling.wgsl` and `shaders/main.wgsl`. See [ray-casting protocol](raycast.md) for geometric visibility and [tree design](tree.md) for resident geometry and refinement.

## Path tracing

Path tracing freezes the camera and pauses CPU splits and local merging. GPU rays do not request refinement while accumulating. Completed feedback can be discarded without applying it. The resident SVO and camera-dependent LOD remain stable; missing detail is rendered as coarse solid geometry even for shadow and continuation rays. Returning to interactive mode resumes streaming.

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

Path samples are averaged in linear light. Mode switches, camera changes, render-resolution changes, and tree replacement reset accumulation. At `2^24` accumulated samples, the running average restarts to keep its count exactly representable in shader floating-point arithmetic.

Color and beam data reside in storage buffers. The presentation shader bilinearly samples the color buffer and applies gamma 2.2 encoding through a non-sRGB target view. Screenshot export uses the same gamma curve. Display/export clamps out-of-range values; transport tests check that linear radiance is finite and nonnegative before display conversion.

## Evaluating MCMC sampling

Markov chain Monte Carlo (MCMC) for rendering commonly takes the form of Metropolis Light Transport (MLT). It mutates light-carrying paths and concentrates work near high-contribution paths. This can help when useful paths are rare, including indirect illumination through small openings, difficult visibility, or glossy transport. [Metropolis Light Transport](https://graphics.stanford.edu/papers/metro/)

MLT is not the recommended default for this diffuse terrain, sky, and sun workload. It requires chain initialization, normalization, mutation tuning, and management of correlated samples. Camera changes invalidate accumulated results and require reassessing chain state. [PBRT: Metropolis Light Transport](https://www.pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/Metropolis_Light_Transport)

Shared random dimensions across pixels are a separate design choice from MCMC: they do not create a Markov chain over light paths. If difficult indirect-light scenes become a target, evaluate primary sample space MLT (PSSMLT) as an offline option against the MIS path tracer. Compare equal-time image error, acceptance rate, effective sample size, and coverage of bright paths using many independent chains.

## Validation and experiments

The GPU smoke suite checks hemisphere and cone distributions, seed/dimension separation, shared bounce samples, MIS against analytic integrals, closed/open-room visibility, finite radiance, deterministic replay, linear accumulation, and reset behavior. Run `cargo test --release --locked --test smoke -- --ignored --nocapture` on a graphics adapter. Sampling diagnostics live in `tests/support/sampling.rs` and `tests/support/sampling.wgsl`.

Compare equal-time image error against a high-sample reference with fixed scene, camera, seed, exposure, and sample mode. Measure coherent versus per-pixel sampling before choosing a default; scrambled low-discrepancy sequences are another candidate. Environment importance sampling, adaptive sampling, and denoising should retain analytic lighting regression tests. Frozen accumulation converges to the resident, camera-LOD approximation; incomplete traversal can introduce dark bias independently of the estimator.
