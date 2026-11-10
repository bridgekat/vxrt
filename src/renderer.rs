mod readback;
mod requests;
mod streaming;
mod uploads;

use crate::{config::Config, core::tree::Tree};
use anyhow::{Result, ensure};
use bytemuck::{Pod, Zeroable};
use cgmath::{Deg, Matrix4, SquareMatrix, Vector3, perspective};
use streaming::Streaming;
pub use streaming::StreamingStats;
use wgpu::util::DeviceExt;

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct Params {
    projection: [[f32; 4]; 4],
    model_view: [[f32; 4]; 4],
    camera: [f32; 4],
    frame: [u32; 4],
    beam: [u32; 4],
    scene: [u32; 4],
    lighting: [f32; 4],
    sampling: [u32; 4],
}
const _: () = {
    assert!(std::mem::size_of::<Params>() == 224);
    assert!(std::mem::offset_of!(Params, camera) == 128);
    assert!(std::mem::offset_of!(Params, lighting) == 192);
    assert!(std::mem::offset_of!(Params, sampling) == 208);
};
pub struct Renderer {
    pub device: wgpu::Device,
    pub queue: wgpu::Queue,
    config: Config,
    pipeline: wgpu::ComputePipeline,
    present: wgpu::RenderPipeline,
    uniforms: [wgpu::Buffer; 2],
    streaming: Streaming,
    frame: wgpu::Buffer,
    beams: wgpu::Buffer,
    groups: Vec<wgpu::BindGroup>,
    present_group: wgpu::BindGroup,
    pub width: u32,
    pub height: u32,
    pub samples: u32,
    last_path: bool,
    last_camera: Option<([f32; 3], [f32; 2])>,
}
impl Renderer {
    pub async fn new(
        adapter: &wgpu::Adapter,
        config: Config,
        width: u32,
        height: u32,
        format: wgpu::TextureFormat,
    ) -> Result<Self> {
        config.validate()?;
        let limits = adapter.limits();
        let required = wgpu::Limits {
            max_storage_buffer_binding_size: limits.max_storage_buffer_binding_size,
            max_buffer_size: limits.max_buffer_size,
            ..Default::default()
        };
        let (device, queue) = adapter
            .request_device(&wgpu::DeviceDescriptor {
                label: Some("vxrt"),
                required_limits: required,
                ..Default::default()
            })
            .await?;
        tracing::info!(adapter=?adapter.get_info(),"GPU selected");
        let mut streaming = Streaming::new(&device, &config);
        let uniforms = std::array::from_fn(|_| {
            device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("frame parameters"),
                contents: bytemuck::bytes_of(&Params::zeroed()),
                usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            })
        });
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("octree renderer"),
            source: wgpu::ShaderSource::Wgsl(crate::shaders::TRACE.into()),
        });
        let pipeline = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
            label: Some("octree renderer"),
            layout: None,
            module: &shader,
            entry_point: Some("main"),
            compilation_options: Default::default(),
            cache: None,
        });
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("presentation"),
            source: wgpu::ShaderSource::Wgsl(crate::shaders::PRESENT.into()),
        });
        let present = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("presentation"),
            layout: None,
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[],
            },
            primitive: Default::default(),
            depth_stencil: None,
            multisample: Default::default(),
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState {
                    format,
                    blend: None,
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            multiview_mask: None,
            cache: None,
        });
        let (width, height) = Self::dimensions(&config, width, height);
        let (frame, beams) = Self::buffers(&device, width, height)?;
        streaming.requests.resize(&device, width, height)?;
        let groups = Self::groups(&device, &pipeline, &uniforms, &streaming, &frame, &beams);
        let present_group = Self::present_group(&device, &present, &uniforms[1], &frame);
        Ok(Self {
            device,
            queue,
            config,
            pipeline,
            present,
            uniforms,
            streaming,
            frame,
            beams,
            groups,
            present_group,
            width,
            height,
            samples: 0,
            last_path: false,
            last_camera: None,
        })
    }
    fn dimensions(config: &Config, w: u32, h: u32) -> (u32, u32) {
        (
            if config.render_width > 0 {
                config.render_width
            } else {
                w.max(1)
            },
            if config.render_height > 0 {
                config.render_height
            } else {
                h.max(1)
            },
        )
    }
    fn buffers(
        device: &wgpu::Device,
        width: u32,
        height: u32,
    ) -> Result<(wgpu::Buffer, wgpu::Buffer)> {
        let size = u64::from(width)
            .checked_mul(u64::from(height))
            .and_then(|pixels| pixels.checked_mul(16))
            .ok_or_else(|| anyhow::anyhow!("render resolution overflows buffer size"))?;
        ensure!(
            size <= device.limits().max_storage_buffer_binding_size
                && size <= device.limits().max_buffer_size,
            "render resolution exceeds GPU buffer limits"
        );
        ensure!(
            width.div_ceil(8) <= device.limits().max_compute_workgroups_per_dimension
                && height.div_ceil(8) <= device.limits().max_compute_workgroups_per_dimension,
            "render resolution exceeds compute dispatch limits"
        );
        let buffer = |label, size| {
            device.create_buffer(&wgpu::BufferDescriptor {
                label: Some(label),
                size,
                usage: wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_SRC,
                mapped_at_creation: false,
            })
        };
        Ok((
            buffer("linear frame", size),
            buffer(
                "coarse beams",
                u64::from(width.div_ceil(4) + 1) * u64::from(height.div_ceil(4) + 1) * 4,
            ),
        ))
    }
    fn groups(
        device: &wgpu::Device,
        pipeline: &wgpu::ComputePipeline,
        uniforms: &[wgpu::Buffer; 2],
        streaming: &Streaming,
        frame: &wgpu::Buffer,
        beams: &wgpu::Buffer,
    ) -> Vec<wgpu::BindGroup> {
        uniforms
            .iter()
            .map(|u| {
                device.create_bind_group(&wgpu::BindGroupDescriptor {
                    label: Some("compute resources"),
                    layout: &pipeline.get_bind_group_layout(0),
                    entries: &[
                        wgpu::BindGroupEntry {
                            binding: 0,
                            resource: u.as_entire_binding(),
                        },
                        wgpu::BindGroupEntry {
                            binding: 1,
                            resource: streaming.gpu.branches.as_entire_binding(),
                        },
                        wgpu::BindGroupEntry {
                            binding: 2,
                            resource: frame.as_entire_binding(),
                        },
                        wgpu::BindGroupEntry {
                            binding: 3,
                            resource: beams.as_entire_binding(),
                        },
                        wgpu::BindGroupEntry {
                            binding: 4,
                            resource: streaming.gpu.data.as_entire_binding(),
                        },
                        wgpu::BindGroupEntry {
                            binding: 5,
                            resource: streaming.requests.pixels.as_entire_binding(),
                        },
                    ],
                })
            })
            .collect()
    }
    fn present_group(
        device: &wgpu::Device,
        pipeline: &wgpu::RenderPipeline,
        uniform: &wgpu::Buffer,
        frame: &wgpu::Buffer,
    ) -> wgpu::BindGroup {
        device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("presentation resources"),
            layout: &pipeline.get_bind_group_layout(0),
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: uniform.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: frame.as_entire_binding(),
                },
            ],
        })
    }
    pub fn resize(&mut self, w: u32, h: u32) -> Result<()> {
        let (w, h) = Self::dimensions(&self.config, w, h);
        if (w, h) == (self.width, self.height) {
            return Ok(());
        }
        let (frame, beams) = Self::buffers(&self.device, w, h)?;
        self.streaming.requests.resize(&self.device, w, h)?;
        self.frame = frame;
        self.beams = beams;
        self.width = w;
        self.height = h;
        self.samples = 0;
        self.groups = Self::groups(
            &self.device,
            &self.pipeline,
            &self.uniforms,
            &self.streaming,
            &self.frame,
            &self.beams,
        );
        self.present_group =
            Self::present_group(&self.device, &self.present, &self.uniforms[1], &self.frame);
        Ok(())
    }
    pub fn render(
        &mut self,
        position: Vector3<f32>,
        rotation: [f32; 2],
        path: bool,
        profiler: bool,
        sample_index: u32,
        target: Option<&wgpu::TextureView>,
    ) -> Result<()> {
        self.streaming.process(&self.device, &self.config, path)?;
        if !path {
            self.streaming
                .unload(&self.config, position.into(), self.height);
        }
        let mut encoder = self.device.create_command_encoder(&Default::default());
        if self.streaming.sync(&self.device, &mut encoder) {
            self.groups = Self::groups(
                &self.device,
                &self.pipeline,
                &self.uniforms,
                &self.streaming,
                &self.frame,
                &self.beams,
            );
        }
        let collect_requests = !path && !self.streaming.feedback_pending();
        let camera = ([position.x, position.y, position.z], rotation);
        if path != self.last_path || (path && self.last_camera != Some(camera)) {
            self.samples = 0;
            self.last_path = path;
        }
        self.last_camera = Some(camera);
        // Keep the accumulation count exactly representable in the shader's f32 arithmetic.
        if self.samples == 1 << 24 {
            self.samples = 0;
        }
        let projection = perspective(
            Deg(self.config.field_of_view),
            self.width as f32 / self.height as f32,
            0.1,
            256.0,
        )
        .invert()
        .unwrap();
        let model_view =
            Matrix4::from_angle_y(Deg(rotation[1])) * Matrix4::from_angle_x(Deg(rotation[0]));
        let mut params = Params {
            projection: projection.into(),
            model_view: model_view.into(),
            camera: [
                position.x,
                position.y,
                position.z,
                self.config.field_of_view.to_radians(),
            ],
            frame: [
                self.width,
                self.height,
                u32::from(path),
                u32::from(profiler),
            ],
            beam: [1, 1, 0, 4],
            scene: [
                self.config.world_levels,
                self.config.march_budget,
                self.streaming.tree.root(),
                u32::from(collect_requests),
            ],
            lighting: [
                self.config.lod_quality,
                self.config.sky_intensity,
                self.config.sun_intensity,
                0.0,
            ],
            sampling: [
                sample_index,
                self.config.seed,
                self.samples,
                u32::from(self.config.coherent_sampling),
            ],
        };
        self.queue
            .write_buffer(&self.uniforms[0], 0, bytemuck::bytes_of(&params));
        params.beam = [if path { 1 } else { 0 }, 4, 1, 1];
        self.queue
            .write_buffer(&self.uniforms[1], 0, bytemuck::bytes_of(&params));
        // Separate passes provide storage visibility between coarse and fine dispatches.
        for i in 0..2 {
            // Jittered path rays trace from the camera; beam skipping is an interactive optimization.
            if path && i == 0 {
                continue;
            }
            let mut pass = encoder.begin_compute_pass(&Default::default());
            pass.set_pipeline(&self.pipeline);
            pass.set_bind_group(0, &self.groups[i], &[]);
            let (w, h) = if i == 0 {
                (self.width.div_ceil(4) + 1, self.height.div_ceil(4) + 1)
            } else {
                (self.width, self.height)
            };
            pass.dispatch_workgroups(w.div_ceil(8), h.div_ceil(8), 1);
        }
        if let Some(view) = target {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("present"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view,
                    resolve_target: None,
                    depth_slice: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                ..Default::default()
            });
            pass.set_pipeline(&self.present);
            pass.set_bind_group(0, &self.present_group, &[]);
            pass.draw(0..3, 0..1);
        }
        let feedback = if path {
            None
        } else {
            self.streaming.capture(&self.device, &mut encoder)
        };
        self.queue.submit([encoder.finish()]);
        if let Some((buffer, tx)) = feedback {
            buffer
                .slice(..)
                .map_async(wgpu::MapMode::Read, move |result| {
                    let _ = tx.send(result);
                });
        }
        self.samples = if path {
            self.samples.saturating_add(1)
        } else {
            0
        };
        Ok(())
    }
    pub fn tree_stats(&self) -> crate::core::tree::TreeStats {
        self.streaming.tree.stats()
    }
    pub fn streaming_stats(&self) -> StreamingStats {
        self.streaming.stats
    }
    pub fn feedback_pending(&self) -> bool {
        self.streaming.feedback_pending()
    }
    pub fn check_tree(&self) -> Result<(u32, usize)> {
        let reachable = self.streaming.tree.validate()?;
        let branches = self.read_buffer(
            &self.streaming.gpu.branches,
            self.streaming.gpu.branches.size(),
        )?;
        let data = self.read_buffer(&self.streaming.gpu.data, self.streaming.gpu.data.size())?;
        self.streaming
            .gpu
            .verify(&self.streaming.tree, &branches, &data)?;
        Ok((reachable as u32, reachable))
    }
    /// Replace the procedural scene with an exact packed tree (streaming disabled).
    pub fn replace_packed_scene(&mut self, words: &[u32]) -> Result<()> {
        let tree = Tree::from_packed(words, self.config.world_levels, self.streaming.limit)?;
        self.streaming.replace(tree);
        self.samples = 0;
        Ok(())
    }
}
#[cfg(test)]
#[path = "../tests/support/renderer_lod.rs"]
mod lod_tests;
