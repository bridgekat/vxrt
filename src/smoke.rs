//! Actual GPU execution, including unaligned viewport edges and accumulation lifecycle.
use crate::{config::Config, renderer::Renderer};
use anyhow::{Result, ensure};
use cgmath::Vector3;
pub async fn run() -> Result<()> {
    let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_without_display_handle());
    let adapter = instance
        .request_adapter(&wgpu::RequestAdapterOptions::default())
        .await?;
    for (dynamic, coherent_sampling) in [(false, true), (true, true), (false, false), (true, false)]
    {
        let config = Config {
            dynamic,
            max_nodes: 65536,
            coherent_sampling,
            ..Default::default()
        };
        let mut r =
            Renderer::new(&adapter, config, 65, 37, wgpu::TextureFormat::Rgba8Unorm).await?;
        if !dynamic && coherent_sampling {
            crate::sampling_checks::check(&r).await?;
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
            r.render(position, rotation, false, false, i, Some(&view));
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
        // Compact at a GPU synchronization point. Dynamic generation remains enabled.
        r.compact_tree()?;
        r.render(position, rotation, false, false, 0, None);
        if !dynamic {
            ensure!(
                r.read_frame()? == pixels,
                "compaction changed static rendering"
            );
        }
        r.render(position, rotation, true, false, 0, None);
        let a = r.read_frame()?;
        let frozen_tree = r.check_tree()?;
        r.render(position, rotation, true, false, 1, None);
        let average = r.read_frame()?;
        ensure!(r.samples == 2, "path sample counter");
        r.samples = 0;
        r.render(position, rotation, true, false, 1, None);
        let b = r.read_frame()?;
        ensure!(
            r.check_tree()? == frozen_tree,
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
        {
            for ((a, b), avg) in a.iter().zip(&b).zip(&average) {
                for c in 0..3 {
                    ensure!(
                        (avg[c] - (a[c] + b[c]) * 0.5).abs() < 1e-5,
                        "accumulation mismatch"
                    );
                }
            }
        }
        r.samples = 0;
        r.render(position, rotation, true, false, 0, None);
        ensure!(r.read_frame()? == a, "sample replay is not deterministic");
        r.render(position, [-40.0, 0.0], true, false, 0, None);
        ensure!(r.samples == 1, "camera change failed to reset accumulation");
        r.render(position, rotation, false, false, 0, None);
        ensure!(r.samples == 0, "interactive mode must not accumulate");
        if !dynamic {
            ensure!(
                r.read_frame()? == pixels,
                "path history leaked into interactive rendering"
            );
        }
        r.resize(64, 32)?;
        ensure!(r.samples == 0, "resize must reset samples");
        r.render(position, rotation, true, false, 2, None);
        ensure!(r.samples == 1, "first sample after resize");
        ensure!(
            r.read_frame()?.iter().flatten().all(|f| f.is_finite()),
            "non-finite path output"
        );
        r.resize(321, 181)?;
        r.render(position, rotation, false, false, 0, None);
        std::fs::create_dir_all("target/smoke")?;
        r.save_png(format!(
            "target/smoke/{dynamic}-{coherent_sampling}-interactive.png"
        ))?;
        for i in 0..32 {
            r.render(position, rotation, true, false, i, None);
        }
        r.save_png(format!(
            "target/smoke/{dynamic}-{coherent_sampling}-path.png"
        ))?;
        check_enclosed_room(&mut r)?;
        tracing::info!(
            dynamic,
            coherent_sampling,
            allocated,
            reachable,
            "GPU smoke passed"
        );
    }
    let mut tiny = Renderer::new(
        &adapter,
        Config {
            dynamic: true,
            max_nodes: 9,
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
        );
    }
    ensure!(
        tiny.check_tree()?.0 <= 9,
        "dynamic allocator exceeded its capacity"
    );
    ensure!(
        tiny.read_frame()?.iter().flatten().all(|f| f.is_finite()),
        "exhausted tree produced non-finite output"
    );
    tiny.resize(1, 1)?;
    tiny.render(
        Vector3::new(128.0, 258.0, 240.0),
        [-45.0, 0.0],
        true,
        false,
        0,
        None,
    );
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
                dynamic: true,
                max_nodes: 9,
                march_budget,
                ..Default::default()
            },
            1,
            1,
            wgpu::TextureFormat::Rgba8Unorm,
        )
        .await?;
        r.write_tree(&[1, 3])?;
        r.render(
            Vector3::new(128.0, 128.0, 128.0),
            [0.0, 0.0],
            true,
            false,
            0,
            None,
        );
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
        "GPU smoke passed: static/dynamic rendering, accumulation, resize, compaction, allocation exhaustion, and configurable march budget. Images: target/smoke/"
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
        r.write_tree(&words)?;
        for index in 0..8 {
            r.render(
                Vector3::new(128.0, 128.0, 128.0),
                [30.0, 0.0],
                true,
                false,
                index,
                None,
            );
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
