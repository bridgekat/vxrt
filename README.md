# Voxel Raycasting Test

A Rust voxel ray-casting and path-tracing renderer built around sparse voxel octrees, with the design philosophy that spatially localized operations should be done locally. Licensed under WTFPLv2.

## Build and run

Install a current stable Rust toolchain and a graphics driver supporting compute shaders and fragment storage buffers. Rendering uses wgpu with Vulkan, DirectX 12, or Metal. The application uses Rust 2024, winit 0.30, wgpu 29, cgmath, bytemuck, serde/TOML, tracing, pollster, image, and egui 0.34.

```sh
cargo run --release --locked
```

Shaders are embedded in the executable. Configuration and screenshot paths are relative to the working directory. Use a release build for interactive performance and faster terrain generation.

## Rendering modes

Interactive mode renders a fresh frame every time. Press **P** to freeze the camera and resident tree and accumulate path-tracing samples in linear light; press P again to return to interactive rendering. Accumulation resets when switching modes, changing the camera or render resolution, or replacing the scene.

A height map and min/max pyramid are baked on the CPU at startup. Interactive rays request octree refinement; the CPU splits nodes, merges compatible leaves, and unloads distant detail within a bounded CPU budget. Missing detail appears as coarse solid geometry. Path tracing pauses loading and merging so accumulation uses a stable tree; explore the scene before pressing P. Geometry uses plain white surfaces without a grid overlay. Interactive rendering uses directional shading; surfaces have constant neutral albedo 0.8, with colored sky and sun illumination in path tracing.

Path tracing uses cosine-weighted Lambertian bounces, sun next-event estimation with multiple importance sampling, and Russian roulette. Bounce samples are spatially shared by default to encourage coherent rays; camera jitter is per-pixel. Set `coherent_sampling = false` to try per-pixel bounce samples and compare the noise/performance tradeoff.

## Controls

| Control | Action |
| --- | --- |
| WASD / mouse | Move / look |
| Space / left Ctrl | Ascend / descend |
| Tab | 256× movement acceleration |
| P | Toggle accumulating path tracing |
| L | Toggle mouse capture |
| M (hold) | Show per-pixel traversal cost in interactive mode |
| C | Check tree topology, consistency and compactness, then compare resident slots with GPU readback |
| O | Save a gamma-encoded PNG in `screenshots/` |
| Escape | Quit |

An egui overlay shows branch/data pool usage and capacity, free slots, exact empty/solid leaves, deferred solid previews, GPU pool storage, baked height-bound storage, and feedback status. Per-frame counters show unique GPU refinement requests received and logical nodes updated, along with successful splits and requests held back by the split budget or pool capacity. Feedback is asynchronous, so the overlay identifies its source frame and distinguishes an empty batch from no batch received. Tree counts update every frame from incrementally maintained counters, without tree scans or GPU readback. Press C for validated tree statistics in the log. Screenshots saved with O contain the scene without the overlay.

Refinement follows screen-space LOD requests without a distance cutoff. A bounded rolling CPU sweep unloads detail beyond the radial LOD retention range. Nearby off-screen detail stays resident, independently of camera orientation; set `max_unload_checks = 0` to disable unloading. Every branch stores a consistent summary of its children. Exact compatible-child merging frees redundant slots; LOD stops and explicit derefinement use the same stored summary.

Movement is always free flight. There is no gravity or collision detection.

## Configuration

The application reads `config.toml` at startup and uses defaults for omitted fields. Unknown keys and invalid values are rejected. Generate a complete configuration with:

```sh
cargo run --locked -- --print-config > config.toml
```

| Key | Default | Meaning |
| --- | --- | --- |
| `world_levels` | `10` | World side length is `2^world_levels` voxels; allowed range 1–12 (up to 4096) |
| `splits_per_frame` | `1024` | Maximum successful CPU splits per frame receiving feedback; allowed range 1–4096. Each split updates several node records |
| `max_unload_checks` | `16384` | Maximum branch slots examined for unloading per interactive frame; 0 disables, maximum 65536 |
| `unloads_per_frame` | `8192` | Maximum logical nodes removed per interactive frame; 0 disables, maximum 65536. Each eligible subtree unload removes eight children |
| `max_split_requests` | `16384` | Maximum raw request entries per readback packet, including duplicates; allowed range 1–1048576. CPU deduplicates before splitting |
| `max_height` | `256` | Exclusive voxel-height cutoff; voxels at or above it are empty |
| `lod_quality` | `1.0` | Positive detail-quality factor; larger values retain finer detail |
| `march_budget` | `1024` | Maximum traversal iterations per ray; allowed range 1–65536 |
| `field_of_view` | `90.0` | Vertical field of view in degrees; at least 1 and less than 179 |
| `render_width` | `0` | Render width in pixels; zero follows the window |
| `render_height` | `0` | Render height in pixels; zero follows the window |
| `seed` | `1` | Seed for path samples; terrain geometry uses a fixed height function |
| `coherent_sampling` | `true` | Share bounce samples across pixels; `false` decorrelates them. Camera jitter is always per-pixel |
| `sky_intensity` | `1.0` | Nonnegative linear multiplier for sky radiance outside the sun disk; zero makes it black |
| `sun_intensity` | `256.0` | Nonnegative linear multiplier for the sun disk radiance |

Baking the height pyramid takes space proportional to the square of the world side length. Resident geometry uses separate branch and data pools: 36 bytes per child block and 16 bytes per node (including branches) on the GPU, plus CPU metadata and GPU feedback. Pools start at 16 slots and double when needed, reusing freed slots through freelists; capacity is retained after merging. Adapter buffer limits cap growth, leaving missing detail as coarse solid geometry if no further allocation fits. Rendering workgroups are 8×8; scattered upload workgroups contain 64 invocations.

The default `march_budget` is `1024`. To adjust it, set `march_budget` in `config.toml`, then restart the application. It applies to interactive, beam, path, and shadow rays; the profiler normalizes cost against this budget. Larger values can reduce dark truncation artifacts but increase GPU work and frame time. They do not change the path bounce count or remove the 20-entry traversal-stack limit.

## Validation

```sh
cargo fmt --all -- --check
cargo test --locked
cargo clippy --locked --all-targets -- -D warnings
cargo test --release --locked --lib --test smoke -- --ignored --nocapture
```

Integration tests check height bounds and occupancy, local merge compatibility, pool growth and delayed-feedback invalidation, tree refinement and residency retention, packed-tree imports, configuration validation, and WGSL shader validity. GPU smoke tests are ignored by default, require a graphics adapter, and run without a window. They execute compute and presentation passes, check sampler distributions and analytic lighting integrals, test closed/open-room visibility, exercise both spatial sampling modes, and verify deterministic replay, accumulation, frozen geometry, compactness checks, and reset behavior. Streaming checks exercise GPU split feedback, scattered uploads, pool growth, bounded unloading after moving away and reloading on return, and the egui stats overlay. Fixed-view checks verify that requests and uploads settle to zero, including from the elevated starting position, and compare settled views against exact generated terrain. Preview PNGs are written to `target/smoke/`.

To isolate sun illumination, set `sky_intensity = 0.0` in `config.toml` and enter path-tracing mode. The defaults are `sky_intensity = 1.0` and `sun_intensity = 256.0`, making sunlight apparent despite the sun disk's small angular size. Adjusting these intensities does not change the sun's angular size or sampling PDFs.

CPU terrain and tree algorithms live in `src/core/`. Tree storage/feedback encoding, import, and explicit diagnostics are private submodules of `core::tree`; topology mutations share one local normalization rule. GPU streaming schedules refinement, while `renderer::uploads` owns pool buffers and scatter writes. Rendering and pass ordering live in `src/renderer.rs`. The packed scene format is an import/reference representation, separate from the runtime pools.

See [tree design](docs/tree.md) for allocation and bounded request feedback, [ray-casting protocol](docs/raycast.md) for traversal and performance measurement, and [sampling](docs/sampling.md) for light transport and accumulation.
