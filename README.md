# Voxel Raycasting Test

A Rust voxel ray-casting and path-tracing renderer built around sparse voxel octrees. Licensed under WTFPLv2.

## Build and run

Install a current stable Rust toolchain and a graphics driver supporting compute shaders and fragment storage buffers. Rendering uses wgpu with Vulkan, DirectX 12, or Metal. The application uses Rust 2024, winit 0.30, wgpu 29, cgmath, bytemuck, serde/TOML, tracing, pollster, and image.

```sh
cargo run --release --locked
```

Shaders are embedded in the executable. Configuration and screenshot paths are relative to the working directory. Use a release build for interactive performance and faster terrain generation.

## Rendering modes

Interactive mode renders a fresh frame every time. Press **P** to freeze the camera and accumulate path-tracing samples in linear light; press P again to return to interactive rendering. Accumulation resets when switching modes, changing the camera or render resolution, or compacting the octree.

Terrain can be precomputed on the CPU or generated dynamically on the GPU. Explore dynamic terrain in interactive mode before pressing P: path tracing freezes the generated tree and treats unresolved regions as coarse opaque cells. Returning to interactive mode resumes generation.

Path tracing uses cosine-weighted Lambertian bounces, sun next-event estimation with multiple importance sampling, and Russian roulette. Bounce samples are spatially shared by default to encourage coherent rays; camera jitter is per-pixel. Set `coherent_sampling = false` to try per-pixel bounce samples and compare the noise/performance tradeoff.

## Controls

| Control | Action |
| --- | --- |
| WASD / mouse | Move / look |
| Space / left Ctrl | Ascend / descend while flying |
| Tab | 256× movement acceleration |
| P | Toggle accumulating path tracing |
| L | Toggle mouse capture |
| M (hold) | Show per-pixel traversal cost in interactive mode |
| C / G | Check / compact the GPU octree using synchronous readback |
| O | Save a gamma-encoded PNG in `screenshots/` |
| F1 | Toggle flying and gravity modes |
| F4 | Toggle forced free flight |
| F2 / Escape | Toggle borderless fullscreen / quit |

Flying is enabled by default. Collision detection is not implemented, so gravity mode falls through terrain.

## Configuration

The application reads `config.toml` at startup and uses defaults for omitted fields. Unknown keys and invalid values are rejected. Generate a complete configuration with:

```sh
cargo run --locked -- --print-config > config.toml
```

| Key | Default | Meaning |
| --- | --- | --- |
| `dynamic` | `false` | Generate terrain lazily on the GPU instead of precomputing it on the CPU |
| `world_levels` | `8` | World side length is `2^world_levels` voxels; maximum 10 for static terrain or 20 for dynamic terrain |
| `max_height` | `256` | Height cutoff used when constructing static terrain |
| `max_nodes` | `16777216` | Dynamic node capacity; each node occupies four bytes |
| `noise_levels` | `8` | Dynamic noise texture side length is `2^noise_levels`; allowed range 1–12 |
| `partial_levels` | `4` | Dynamic terrain noise offset/detail parameter; allowed range 0–8 |
| `lod_quality` | `0.5` | Positive detail-quality factor; larger values retain finer detail |
| `field_of_view` | `70.0` | Vertical field of view in degrees; at least 1 and less than 179 |
| `render_width` | `0` | Render width in pixels; zero follows the window |
| `render_height` | `0` | Render height in pixels; zero follows the window |
| `seed` | `1` | Seed for path samples and dynamic terrain noise; static terrain geometry uses a fixed height function |
| `coherent_sampling` | `true` | Share bounce samples across pixels; `false` decorrelates them. Camera jitter is always per-pixel |

`noise_levels` must not exceed `world_levels + partial_levels`. Static generation grows cubically with world size. The default dynamic allocation budget occupies 64 MiB plus a four-byte header. GPU buffer limits also constrain the node budget and render resolution.

For a larger dynamically generated world:

```toml
dynamic = true
world_levels = 16
noise_levels = 8
partial_levels = 4
max_nodes = 16777216
lod_quality = 0.5
```

When the dynamic node budget is exhausted, unresolved nodes appear as coarse opaque placeholders. Increase `max_nodes` or reduce detail to avoid them. Compute workgroups are fixed at 8×8.

## Validation

```sh
cargo fmt --all -- --check
cargo test --locked
cargo clippy --locked --all-targets -- -D warnings
cargo run --release --locked -- --smoke-test
```

Unit tests check the packed tree against the terrain height field, noise bounds, configuration validation, and WGSL shader validity. GPU smoke tests require a graphics adapter and run without a window. They execute compute and presentation passes, check sampler distributions and analytic lighting integrals, test closed/open-room visibility, exercise both spatial sampling modes, and verify deterministic replay, accumulation, frozen dynamic trees, allocation exhaustion, and reset behavior. Preview PNGs are written to `target/smoke/`.

See [rendering architecture and development priorities](docs/rendering.md) for algorithm details, limitations, and sampling recommendations.
