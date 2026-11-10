//! Opt-in, headless steady-state throughput measurement; not an FPS assertion.
use anyhow::{Result, ensure};
use cgmath::Vector3;
use std::{hash::Hasher, time::Instant};
use vxrt::{
    config::Config,
    renderer::Renderer,
    stats_overlay::{OverlayStats, StatsOverlay},
};

#[test]
#[ignore = "requires an idle GPU; run with --release --test performance -- --ignored --nocapture"]
fn steady_state() -> Result<()> {
    pollster::block_on(async {
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_without_display_handle());
        let adapter = instance.request_adapter(&Default::default()).await?;
        println!("Adapter: {:?}", adapter.get_info());
        let mut renderer = Renderer::new(
            &adapter,
            Config::default(),
            1280,
            720,
            wgpu::TextureFormat::Rgba8Unorm,
        )
        .await?;
        let target = renderer.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("benchmark presentation"),
            size: wgpu::Extent3d {
                width: 1280,
                height: 720,
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
        let mut overlay = std::env::var_os("VXRT_PERF_OVERLAY")
            .map(|_| StatsOverlay::new(&renderer.device, wgpu::TextureFormat::Rgba8Unorm));
        println!("Overlay enabled: {}", overlay.is_some());
        for (name, position, rotation) in [
            ("elevated", Vector3::new(512., 1026., 512.), [-80., 0.]),
            ("terrain", Vector3::new(512., 300., 900.), [-20., 0.]),
        ] {
            // Drain each warmup frame so asynchronous refinement can make progress.
            for i in 0..256 {
                renderer.render(position, rotation, false, false, i, Some(&view))?;
                renderer.device.poll(wgpu::PollType::wait_indefinitely())?;
            }
            println!("{name} activity: {:?}", renderer.streaming_stats());
            let activity = renderer.streaming_stats();
            ensure!(
                activity.feedback_frame.is_some()
                    && activity.requested == 0
                    && activity.uploaded_nodes() == 0,
                "view did not settle during warmup"
            );
            let mut hash = std::hash::DefaultHasher::new();
            hash.write(bytemuck::cast_slice(&renderer.read_frame()?));
            println!("{name} linear image hash: {:016x}", hash.finish());
            let mut times = Vec::new();
            let tree = renderer.tree_stats();
            for _ in 0..5 {
                let start = Instant::now();
                for i in 0..120 {
                    renderer.render(position, rotation, false, false, i, Some(&view))?;
                    if let Some(overlay) = &mut overlay {
                        overlay.draw(
                            &renderer,
                            &view,
                            egui::RawInput {
                                screen_rect: Some(egui::Rect::from_min_size(
                                    egui::Pos2::ZERO,
                                    egui::vec2(1280., 720.),
                                )),
                                ..Default::default()
                            },
                            [1280, 720],
                            &OverlayStats {
                                tree,
                                streaming: renderer.streaming_stats(),
                                position: position.into(),
                                feedback_pending: renderer.feedback_pending(),
                                ..Default::default()
                            },
                        );
                    }
                    if i % 4 == 3 {
                        renderer.device.poll(wgpu::PollType::wait_indefinitely())?;
                    }
                }
                times.push(start.elapsed().as_secs_f64() * 1000. / 120.);
            }
            times.sort_by(f64::total_cmp);
            println!(
                "{name}: median {:.3} ms/frame ({:.1} FPS), range {:.3}..{:.3}",
                times[2],
                1000. / times[2],
                times[0],
                times[4]
            );
        }
        Ok(())
    })
}
