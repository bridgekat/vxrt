//! Read-only egui diagnostics, composited after scene presentation.
use crate::{
    core::tree::TreeStats,
    renderer::{Renderer, StreamingStats},
};
#[derive(Clone, Copy, Default)]
pub struct OverlayStats {
    pub tree: TreeStats,
    pub streaming: StreamingStats,
    pub path_tracing: bool,
    pub samples: u32,
    pub fps: f32,
    pub position: [f32; 3],
    pub feedback_pending: bool,
}
pub struct StatsOverlay {
    pub context: egui::Context,
    renderer: egui_wgpu::Renderer,
}
impl StatsOverlay {
    pub fn new(device: &wgpu::Device, format: wgpu::TextureFormat) -> Self {
        let context = egui::Context::default();
        context.global_style_mut(|style| style.interaction.selectable_labels = false);
        Self {
            context,
            renderer: egui_wgpu::Renderer::new(
                device,
                format,
                egui_wgpu::RendererOptions::default(),
            ),
        }
    }
    pub fn draw(
        &mut self,
        renderer: &Renderer,
        view: &wgpu::TextureView,
        input: egui::RawInput,
        size: [u32; 2],
        stats: &OverlayStats,
    ) -> egui::PlatformOutput {
        let output = self.context.run_ui(input, |root| {
            let ctx = root.ctx();
            egui::Area::new("tree_stats".into())
                .fixed_pos(egui::pos2(8., 8.))
                .interactable(false)
                .show(ctx, |ui| {
                    egui::Frame::new()
                        .fill(egui::Color32::from_black_alpha(210))
                        .inner_margin(10.)
                        .corner_radius(6.)
                        .show(ui, |ui| {
                            ui.heading("Tree statistics");
                            ui.label(if stats.path_tracing {
                                format!("Frozen tree  |  {} spp", stats.samples)
                            } else {
                                format!("Refining  |  {:.0} FPS", stats.fps)
                            });
                            ui.separator();
                            let tree = stats.tree;
                            ui.monospace(format!(
                                "Branches {:>8} / {}",
                                tree.branches, tree.branch_capacity
                            ));
                            ui.monospace(format!(
                                "Leaves   {:>8} / {}",
                                tree.leaves, tree.leaf_capacity
                            ));
                            ui.label(format!(
                                "Free slots: {} branches, {} leaves",
                                tree.branch_capacity - tree.branches,
                                tree.leaf_capacity - tree.leaves
                            ));
                            ui.separator();
                            ui.label(format!(
                                "Empty {}   Solid {}",
                                tree.empty_leaves, tree.solid_leaves
                            ));
                            ui.label(format!("Deferred solid previews: {}", tree.deferred_leaves));
                            ui.label(format!(
                                "GPU pools: {:.2} MiB live / {:.2} MiB reserved",
                                tree.gpu_live_bytes() as f64 / 1048576.,
                                tree.gpu_pool_bytes() as f64 / 1048576.
                            ));
                            ui.label(format!(
                                "Baked height bounds: {:.2} MiB",
                                tree.height_map_bytes as f64 / 1048576.
                            ));
                            let activity = stats.streaming;
                            ui.separator();
                            ui.label(format!(
                                "GPU -> CPU: {} requests this frame",
                                activity.requested
                            ));
                            ui.label(format!(
                                "CPU -> GPU: {} updated nodes",
                                activity.uploaded_nodes()
                            ));
                            ui.small(match activity.feedback_frame {
                                Some(frame) => format!(
                                    "Feedback from frame {frame} ({} frames old)",
                                    activity.frame - frame
                                ),
                                None => "No feedback batch received this frame".to_owned(),
                            });
                            ui.small(format!(
                                "Split {} | budget deferred {}",
                                activity.split, activity.budget_deferred
                            ));
                            ui.small(format!(
                                "Pool full {} | stale {}",
                                activity.pool_limited, activity.stale
                            ));
                            ui.label(if stats.path_tracing {
                                "Refinement paused"
                            } else if stats.feedback_pending {
                                "Feedback pending"
                            } else {
                                "Feedback ready"
                            });
                            ui.separator();
                            ui.small("No eviction: generated detail stays resident");
                            ui.small(format!(
                                "Position: {:.1}, {:.1}, {:.1}",
                                stats.position[0], stats.position[1], stats.position[2]
                            ));
                        });
                });
        });
        let screen = egui_wgpu::ScreenDescriptor {
            size_in_pixels: size,
            pixels_per_point: output.pixels_per_point,
        };
        let jobs = self
            .context
            .tessellate(output.shapes, output.pixels_per_point);
        for (id, delta) in &output.textures_delta.set {
            self.renderer
                .update_texture(&renderer.device, &renderer.queue, *id, delta);
        }
        let mut encoder = renderer
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("stats overlay"),
            });
        let callbacks = self.renderer.update_buffers(
            &renderer.device,
            &renderer.queue,
            &mut encoder,
            &jobs,
            &screen,
        );
        {
            // No encoder access until this pass is dropped.
            let mut pass = encoder
                .begin_render_pass(&wgpu::RenderPassDescriptor {
                    label: Some("stats overlay"),
                    color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                        view,
                        resolve_target: None,
                        depth_slice: None,
                        ops: wgpu::Operations {
                            load: wgpu::LoadOp::Load,
                            store: wgpu::StoreOp::Store,
                        },
                    })],
                    ..Default::default()
                })
                .forget_lifetime();
            self.renderer.render(&mut pass, &jobs, &screen);
        }
        renderer
            .queue
            .submit(callbacks.into_iter().chain([encoder.finish()]));
        for id in &output.textures_delta.free {
            self.renderer.free_texture(id);
        }
        output.platform_output
    }
}
