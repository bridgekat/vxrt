//! Actual GPU execution, including unaligned viewport edges and accumulation lifecycle.
mod support;
use anyhow::{Result, ensure};
use cgmath::Vector3;
use vxrt::{config::Config, renderer::Renderer};
#[test]
#[ignore = "requires a graphics adapter; run with --release --test smoke -- --ignored"]
fn gpu_rendering() -> Result<()> {
    pollster::block_on(run())
}

async fn run() -> Result<()> {
    let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_without_display_handle());
    let adapter = instance
        .request_adapter(&wgpu::RequestAdapterOptions::default())
        .await?;
    check_distant_refinement(&adapter).await?;
    check_activity_counters(&adapter).await?;
    check_streaming(&adapter).await?;
    check_settling(&adapter).await?;
    for coherent_sampling in [true, false] {
        let config = Config {
            world_levels: 8,
            coherent_sampling,
            ..Default::default()
        };
        let mut r =
            Renderer::new(&adapter, config, 65, 37, wgpu::TextureFormat::Rgba8Unorm).await?;
        r.replace_packed_scene(&vxrt::core::packed::generate_terrain(8, 256))?;
        if coherent_sampling {
            support::requests::check(&r)?;
            support::sampling::check(&r).await?;
        }
        let target = r.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("smoke presentation"),
            size: wgpu::Extent3d {
                width: 65,
                height: 37,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        });
        let view = target.create_view(&Default::default());
        let position = Vector3::new(128.0, 258.0, 240.0);
        let rotation = [-45.0, 0.0];
        for i in 0..3 {
            r.render(position, rotation, false, false, i, Some(&view))?;
        }
        let pixels = r.read_frame()?;
        ensure!(
            pixels.iter().flatten().all(|f| f.is_finite()),
            "non-finite interactive output"
        );
        ensure!(
            pixels.iter().any(|p| p != &pixels[0]),
            "uniform image: expected terrain and sky"
        );
        let (allocated, reachable) = r.check_tree()?;
        ensure!(allocated >= reachable as u32, "bad tree count");
        // Explicit diagnostics must not change the rendered geometry.
        r.check_tree()?;
        r.render(position, rotation, false, false, 0, None)?;
        ensure!(
            r.read_frame()? == pixels,
            "tree check changed static rendering"
        );
        r.render(position, rotation, true, false, 0, None)?;
        let a = r.read_frame()?;
        let tree_stats = r.check_tree()?;
        r.render(position, rotation, true, false, 1, None)?;
        let average = r.read_frame()?;
        ensure!(r.samples == 2, "path sample counter");
        r.samples = 0;
        r.render(position, rotation, true, false, 1, None)?;
        let b = r.read_frame()?;
        ensure!(
            r.check_tree()? == tree_stats,
            "path tracing mutated the tree"
        );
        ensure!(
            a != b,
            "different sample indices generated identical frames"
        );
        ensure!(
            a.iter()
                .chain(&b)
                .flatten()
                .all(|v| v.is_finite() && *v >= 0.0),
            "invalid path energy"
        );
        for ((a, b), avg) in a.iter().zip(&b).zip(&average) {
            for c in 0..3 {
                ensure!(
                    (avg[c] - (a[c] + b[c]) * 0.5).abs() < 1e-5,
                    "accumulation mismatch"
                );
            }
        }
        r.samples = 0;
        r.render(position, rotation, true, false, 0, None)?;
        ensure!(r.read_frame()? == a, "sample replay is not deterministic");
        r.render(position, [-40.0, 0.0], true, false, 0, None)?;
        ensure!(r.samples == 1, "camera change failed to reset accumulation");
        r.render(position, rotation, false, false, 0, None)?;
        ensure!(r.samples == 0, "interactive mode must not accumulate");
        ensure!(
            r.read_frame()? == pixels,
            "path history leaked into interactive rendering"
        );
        r.resize(64, 32)?;
        ensure!(r.samples == 0, "resize must reset samples");
        r.render(position, rotation, true, false, 2, None)?;
        ensure!(r.samples == 1, "first sample after resize");
        ensure!(
            r.read_frame()?.iter().flatten().all(|f| f.is_finite()),
            "non-finite path output"
        );
        r.resize(321, 181)?;
        r.render(position, rotation, false, false, 0, None)?;
        std::fs::create_dir_all("target/smoke")?;
        r.save_png(format!("target/smoke/{coherent_sampling}-interactive.png"))?;
        for i in 0..32 {
            r.render(position, rotation, true, false, i, None)?;
        }
        r.save_png(format!("target/smoke/{coherent_sampling}-path.png"))?;
        check_enclosed_room(&mut r)?;
        tracing::info!(coherent_sampling, allocated, reachable, "GPU smoke passed");
    }
    let mut tiny = Renderer::new(
        &adapter,
        Config {
            world_levels: 8,
            ..Default::default()
        },
        17,
        13,
        wgpu::TextureFormat::Rgba8Unorm,
    )
    .await?;
    for i in 0..4 {
        tiny.render(
            Vector3::new(128.0, 258.0, 240.0),
            [-45.0, 0.0],
            false,
            false,
            i,
            None,
        )?;
    }
    tiny.resize(1, 1)?;
    tiny.render(
        Vector3::new(128.0, 258.0, 240.0),
        [-45.0, 0.0],
        true,
        false,
        0,
        None,
    )?;
    ensure!(
        tiny.read_frame()?.iter().flatten().all(|f| f.is_finite()),
        "1x1 viewport failed"
    );
    ensure!(
        tiny.resize(u32::MAX, u32::MAX).is_err(),
        "oversized render target accepted"
    );
    // An empty root needs one step to reach its boundary and another to report escape.
    // Check the configured budget through the complete host/uniform/shader path.
    for march_budget in [1, 2] {
        let mut r = Renderer::new(
            &adapter,
            Config {
                world_levels: 1,
                max_height: 2,
                march_budget,
                ..Default::default()
            },
            1,
            1,
            wgpu::TextureFormat::Rgba8Unorm,
        )
        .await?;
        r.replace_packed_scene(&[1, 3])?;
        r.render(
            Vector3::new(1.0, 1.0, 1.0),
            [0.0, 0.0],
            true,
            false,
            0,
            None,
        )?;
        let pixel = r.read_frame()?[0];
        if march_budget == 1 {
            ensure!(pixel[..3] == [0.0; 3], "march budget was not applied");
        } else {
            ensure!(
                pixel[..3].iter().all(|v| v.is_finite() && *v > 0.0),
                "larger march budget did not finish the ray"
            );
        }
    }
    println!(
        "GPU smoke passed: streaming, pool growth, scatter uploads, frozen accumulation, resize, compactness checks, and march budget. Images: target/smoke/"
    );
    Ok(())
}

fn check_enclosed_room(r: &mut Renderer) -> Result<()> {
    fn build(nodes: &mut Vec<u32>, ptr: usize, position: [u32; 3], size: u32, open: bool) {
        if size == 1 {
            let [x, y, z] = position;
            let solid = x == 0 || x == 7 || z == 0 || z == 7 || y == 0 || (!open && y == 7);
            nodes[ptr] = 3 | (u32::from(solid) << 2);
            return;
        }
        let children = nodes.len();
        nodes.resize(children + 8, 0);
        nodes[ptr] = (children as u32) << 2 | 1;
        for i in 0..8 {
            build(
                nodes,
                children + i,
                [
                    position[0] + (i as u32 & 1) * size / 2,
                    position[1] + ((i as u32 >> 1) & 1) * size / 2,
                    position[2] + ((i as u32 >> 2) & 1) * size / 2,
                ],
                size / 2,
                open,
            );
        }
    }
    r.resize(17, 13)?;
    for open in [false, true] {
        let mut words = vec![0];
        build(&mut words, 0, [0; 3], 8, open);
        words.insert(0, words.len() as u32);
        r.replace_packed_scene(&words)?;
        for index in 0..8 {
            r.render(
                Vector3::new(128.0, 128.0, 128.0),
                [30.0, 0.0],
                true,
                false,
                index,
                None,
            )?;
        }
        let frame = r.read_frame()?;
        ensure!(
            frame.iter().flatten().all(|v| v.is_finite() && *v >= 0.0),
            "room transport produced invalid energy"
        );
        if open {
            ensure!(
                frame.iter().any(|p| p[0] > 0.0),
                "open room receives no environment light"
            );
        } else {
            ensure!(
                frame.iter().all(|p| p[..3] == [0.0; 3]),
                "closed non-emissive room leaked environment light"
            );
        }
    }
    Ok(())
}

async fn check_streaming(adapter: &wgpu::Adapter) -> Result<()> {
    let config = Config {
        world_levels: 8,
        splits_per_frame: 256,
        ..Default::default()
    };
    let mut r = Renderer::new(adapter, config, 65, 37, wgpu::TextureFormat::Rgba8Unorm).await?;
    let camera = Vector3::new(128., 258., 240.);
    for i in 0..32 {
        r.render(camera, [-45., 0.], false, false, i, None)?;
        r.read_frame()?;
        r.check_tree()?;
    }
    std::fs::create_dir_all("target/smoke")?;
    r.save_png("target/smoke/streaming.png")?;
    check_overlay(&mut r)?;
    let grown = r.check_tree()?;
    ensure!(grown.0 > 16, "GPU feedback did not refine and grow pools");
    for i in 0..4 {
        r.render(camera, [-45., 0.], true, false, i, None)?;
        r.read_frame()?;
        ensure!(r.check_tree()? == grown, "tree changed while accumulating");
    }
    let far = Vector3::new(4096., 4096., 4096.);
    r.render(far, [0., 0.], false, false, 0, None)?;
    r.read_frame()?;
    ensure!(
        r.check_tree()? == grown,
        "moving away discarded resident detail"
    );
    for i in 0..16 {
        r.render(camera, [-45., 0.], false, false, i, None)?;
        r.read_frame()?;
        r.check_tree()?;
    }
    ensure!(r.check_tree()?.0 > 16, "resident detail was lost");
    Ok(())
}

fn check_overlay(r: &mut Renderer) -> Result<()> {
    use vxrt::stats_overlay::{OverlayStats, StatsOverlay};
    let size = [640, 360];
    let texture = r.device.create_texture(&wgpu::TextureDescriptor {
        label: Some("stats preview"),
        size: wgpu::Extent3d {
            width: size[0],
            height: size[1],
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Rgba8Unorm,
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
        view_formats: &[],
    });
    let view = texture.create_view(&Default::default());
    let mut overlay = StatsOverlay::new(&r.device, wgpu::TextureFormat::Rgba8Unorm);
    let stats = OverlayStats {
        tree: r.tree_stats(),
        streaming: r.streaming_stats(),
        fps: 60.,
        position: [128., 258., 240.],
        ..Default::default()
    };
    for i in 0..2 {
        r.render(
            Vector3::new(128., 258., 240.),
            [-45., 0.],
            true,
            false,
            i,
            Some(&view),
        )?;
        let input = egui::RawInput {
            screen_rect: Some(egui::Rect::from_min_size(
                egui::Pos2::ZERO,
                egui::vec2(size[0] as f32, size[1] as f32),
            )),
            time: Some(i as f64),
            ..Default::default()
        };
        overlay.draw(r, &view, input, size, &stats);
    }
    let buffer = r.device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("overlay pixels"),
        size: u64::from(size[0] * size[1] * 4),
        usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::COPY_SRC,
        mapped_at_creation: false,
    });
    let mut encoder = r.device.create_command_encoder(&Default::default());
    encoder.copy_texture_to_buffer(
        wgpu::TexelCopyTextureInfo {
            texture: &texture,
            mip_level: 0,
            origin: wgpu::Origin3d::ZERO,
            aspect: wgpu::TextureAspect::All,
        },
        wgpu::TexelCopyBufferInfo {
            buffer: &buffer,
            layout: wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(size[0] * 4),
                rows_per_image: Some(size[1]),
            },
        },
        wgpu::Extent3d {
            width: size[0],
            height: size[1],
            depth_or_array_layers: 1,
        },
    );
    r.queue.submit([encoder.finish()]);
    let pixels = r.read_buffer(&buffer, buffer.size())?;
    image::RgbaImage::from_raw(size[0], size[1], pixels)
        .unwrap()
        .save("target/smoke/stats-overlay.png")?;
    Ok(())
}

async fn check_settling(adapter: &wgpu::Adapter) -> Result<()> {
    for (levels, camera, max_split_requests) in [
        (8, Vector3::new(128., 258., 240.), 16384),
        (10, Vector3::new(512., 300., 900.), 16384),
        (8, Vector3::new(128., 258., 240.), 32),
    ] {
        let config = Config {
            world_levels: levels,
            max_split_requests,
            ..Default::default()
        };
        let mut r =
            Renderer::new(adapter, config, 129, 73, wgpu::TextureFormat::Rgba8Unorm).await?;
        let mut quiet = 0;
        for frame in 0..1024 {
            r.render(camera, [-35., 0.], false, false, frame, None)?;
            r.read_frame()?;
            let stats = r.streaming_stats();
            if stats.feedback_frame.is_some() {
                quiet = if stats.requested == 0 && stats.uploaded_nodes() == 0 {
                    quiet + 1
                } else {
                    0
                };
                if quiet >= 4 {
                    break;
                }
            }
        }
        ensure!(
            quiet >= 4,
            "stationary streaming did not settle: {:?}",
            r.streaming_stats()
        );
        let streamed = r.read_frame()?;
        r.replace_packed_scene(&vxrt::core::packed::generate_terrain(levels, 256))?;
        r.render(camera, [-35., 0.], false, false, 0, None)?;
        let exact = r.read_frame()?;
        let changed = streamed
            .iter()
            .zip(&exact)
            .filter(|(a, b)| a.iter().zip(b.iter()).any(|(a, b)| (a - b).abs() > 1e-5))
            .count();
        ensure!(
            changed == 0,
            "settled streamed tree differs from exact terrain in {changed} pixels (levels={levels})"
        );
    }
    Ok(())
}

async fn check_distant_refinement(adapter: &wgpu::Adapter) -> Result<()> {
    let mut r = Renderer::new(
        adapter,
        Config::default(),
        65,
        37,
        wgpu::TextureFormat::Rgba8Unorm,
    )
    .await?;
    let camera = Vector3::new(512., 1026., 512.);
    let mut split = 0;
    let mut quiet = 0;
    for i in 0..256 {
        r.render(camera, [-80., 0.], false, false, i, None)?;
        r.read_frame()?;
        let stats = r.streaming_stats();
        split += stats.split;
        if stats.feedback_frame.is_some() {
            quiet = if stats.requested == 0 && stats.uploaded_nodes() == 0 {
                quiet + 1
            } else {
                0
            };
        }
        if quiet >= 4 {
            break;
        }
    }
    ensure!(
        split > 1 && quiet >= 4,
        "visible distant terrain did not refine and settle: split={split}, latest={:?}",
        r.streaming_stats()
    );
    Ok(())
}

async fn check_activity_counters(adapter: &wgpu::Adapter) -> Result<()> {
    let config = Config {
        world_levels: 8,
        splits_per_frame: 1,
        max_split_requests: 8,
        ..Default::default()
    };
    let mut r = Renderer::new(adapter, config, 33, 19, wgpu::TextureFormat::Rgba8Unorm).await?;
    let camera = Vector3::new(128., 258., 240.);
    let mut saw_budget = false;
    let mut saw_overflow = false;
    let mut saw_duplicates = false;
    for i in 0..24 {
        r.render(camera, [-45., 0.], false, false, i, None)?;
        r.read_frame()?;
        let s = r.streaming_stats();
        ensure!(s.frame == u64::from(i) + 1, "wrong streaming frame number");
        ensure!(
            s.requested
                == s.split
                    + s.budget_deferred
                    + s.pool_limited
                    + s.stale
                    + s.not_needed
                    + s.paused_discarded,
            "unaccounted feedback requests: {s:?}"
        );
        ensure!(s.split <= 1, "split budget exceeded");
        if let Some(frame) = s.feedback_frame {
            ensure!(
                frame < s.frame,
                "feedback frame is not older than consuming frame"
            );
        } else {
            ensure!(
                s.requested == 0,
                "no batch must report no received requests"
            );
        }
        if i == 0 {
            ensure!(s.uploaded_nodes() == 1, "initial root upload count");
        }
        saw_budget |= s.budget_deferred > 0;
        saw_overflow |= s.request_dropped > 0;
        saw_duplicates |= s.request_entries > s.requested;
        ensure!(
            s.request_entries <= 8 && s.requested <= s.request_entries,
            "invalid bounded packet counts"
        );
    }
    ensure!(saw_budget, "did not exercise budget-deferred feedback");
    ensure!(
        saw_overflow && saw_duplicates,
        "did not exercise overflow and CPU deduplication"
    );
    for i in 0..3 {
        r.render(camera, [-45., 0.], true, false, i, None)?;
        r.read_frame()?;
        let s = r.streaming_stats();
        ensure!(
            s.split == 0 && s.uploaded_nodes() == 0,
            "frozen rendering changed nodes"
        );
        if i > 0 {
            ensure!(
                s.requested == 0,
                "request count was not reset after paused feedback discard"
            );
        }
    }
    Ok(())
}
