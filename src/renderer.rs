use crate::{config::Config, world};
use anyhow::{Result, ensure};
use bytemuck::{Pod, Zeroable};
use cgmath::{Deg, Matrix4, SquareMatrix, Vector3, perspective};
use wgpu::util::DeviceExt;

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct Params {
    projection: [[f32; 4]; 4],
    model_view: [[f32; 4]; 4],
    camera: [f32; 4],
    frame: [u32; 4],
    beam: [u32; 4],
    world: [u32; 4],
    misc: [f32; 4],
    sampling: [u32; 4],
    traversal: [u32; 4],
}
const _: () = {
    assert!(std::mem::size_of::<Params>() == 240);
    assert!(std::mem::offset_of!(Params, camera) == 128);
    assert!(std::mem::offset_of!(Params, misc) == 192);
    assert!(std::mem::offset_of!(Params, sampling) == 208);
    assert!(std::mem::offset_of!(Params, traversal) == 224);
};
pub struct Renderer {
    pub device: wgpu::Device,
    pub queue: wgpu::Queue,
    config: Config,
    pipeline: wgpu::ComputePipeline,
    present: wgpu::RenderPipeline,
    uniforms: [wgpu::Buffer; 2],
    tree: wgpu::Buffer,
    output: wgpu::Buffer,
    noise: Vec<wgpu::TextureView>,
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
        let tree_data = if config.dynamic {
            vec![1, 0]
        } else {
            world::generate(config.world_levels, config.max_height)
        };
        let tree_size = if config.dynamic {
            (u64::from(config.max_nodes) + 1) * 4
        } else {
            tree_data.len() as u64 * 4
        };
        ensure!(
            tree_size <= limits.max_storage_buffer_binding_size
                && tree_size <= limits.max_buffer_size,
            "octree needs {tree_size} bytes, exceeding adapter limits; lower max_nodes/world_levels"
        );
        let tree = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("octree"),
            size: tree_size,
            usage: wgpu::BufferUsages::STORAGE
                | wgpu::BufferUsages::COPY_DST
                | wgpu::BufferUsages::COPY_SRC,
            mapped_at_creation: false,
        });
        queue.write_buffer(&tree, 0, bytemuck::cast_slice(&tree_data));
        let output = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("node count"),
            size: 4,
            usage: wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_SRC,
            mapped_at_creation: false,
        });
        let uniforms = std::array::from_fn(|_| {
            device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("frame parameters"),
                contents: bytemuck::bytes_of(&Params::zeroed()),
                usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            })
        });
        let mut noise = Vec::new();
        for mipmaps in world::noise_maps(config.noise_levels, config.seed) {
            let size = 1 << config.noise_levels;
            let tex = device.create_texture(&wgpu::TextureDescriptor {
                label: Some("terrain noise/bounds"),
                size: wgpu::Extent3d {
                    width: size,
                    height: size,
                    depth_or_array_layers: 1,
                },
                mip_level_count: mipmaps.len() as u32,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Rgba8Unorm,
                usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
                view_formats: &[],
            });
            for (level, data) in mipmaps.iter().enumerate() {
                let width = size >> level;
                queue.write_texture(
                    wgpu::TexelCopyTextureInfo {
                        texture: &tex,
                        mip_level: level as u32,
                        origin: wgpu::Origin3d::ZERO,
                        aspect: wgpu::TextureAspect::All,
                    },
                    data,
                    wgpu::TexelCopyBufferLayout {
                        offset: 0,
                        bytes_per_row: Some(width * 4),
                        rows_per_image: Some(width),
                    },
                    wgpu::Extent3d {
                        width,
                        height: width,
                        depth_or_array_layers: 1,
                    },
                );
            }
            noise.push(tex.create_view(&Default::default()));
        }
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
        let groups = Self::groups(
            &device, &pipeline, &uniforms, &tree, &output, &frame, &beams, &noise,
        );
        let present_group = Self::present_group(&device, &present, &uniforms[1], &frame);
        Ok(Self {
            device,
            queue,
            config,
            pipeline,
            present,
            uniforms,
            tree,
            output,
            noise,
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
    #[allow(clippy::too_many_arguments)]
    fn groups(
        device: &wgpu::Device,
        pipeline: &wgpu::ComputePipeline,
        uniforms: &[wgpu::Buffer; 2],
        tree: &wgpu::Buffer,
        output: &wgpu::Buffer,
        frame: &wgpu::Buffer,
        beams: &wgpu::Buffer,
        noise: &[wgpu::TextureView],
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
                            resource: tree.as_entire_binding(),
                        },
                        wgpu::BindGroupEntry {
                            binding: 2,
                            resource: output.as_entire_binding(),
                        },
                        wgpu::BindGroupEntry {
                            binding: 3,
                            resource: frame.as_entire_binding(),
                        },
                        wgpu::BindGroupEntry {
                            binding: 4,
                            resource: wgpu::BindingResource::TextureView(&noise[0]),
                        },
                        wgpu::BindGroupEntry {
                            binding: 5,
                            resource: wgpu::BindingResource::TextureView(&noise[1]),
                        },
                        wgpu::BindGroupEntry {
                            binding: 6,
                            resource: wgpu::BindingResource::TextureView(&noise[2]),
                        },
                        wgpu::BindGroupEntry {
                            binding: 7,
                            resource: beams.as_entire_binding(),
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
        self.frame = frame;
        self.beams = beams;
        self.width = w;
        self.height = h;
        self.samples = 0;
        self.groups = Self::groups(
            &self.device,
            &self.pipeline,
            &self.uniforms,
            &self.tree,
            &self.output,
            &self.frame,
            &self.beams,
            &self.noise,
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
    ) {
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
            world: [
                u32::from(self.config.dynamic),
                self.config.max_nodes,
                self.config.world_levels,
                self.config.noise_levels,
            ],
            misc: [
                self.config.partial_levels as f32,
                self.config.lod_quality,
                self.config.sky_intensity,
                self.config.sun_intensity,
            ],
            sampling: [
                sample_index,
                self.config.seed,
                self.samples,
                u32::from(self.config.coherent_sampling),
            ],
            traversal: [self.config.march_budget, 0, 0, 0],
        };
        self.queue
            .write_buffer(&self.uniforms[0], 0, bytemuck::bytes_of(&params));
        params.beam = [if path { 1 } else { 0 }, 4, 1, 1];
        self.queue
            .write_buffer(&self.uniforms[1], 0, bytemuck::bytes_of(&params));
        let mut encoder = self.device.create_command_encoder(&Default::default());
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
        self.queue.submit([encoder.finish()]);
        self.samples = if path {
            self.samples.saturating_add(1)
        } else {
            0
        };
    }
    pub fn read_frame(&self) -> Result<Vec<[f32; 4]>> {
        let bytes = self.read_buffer(&self.frame, self.frame.size())?;
        Ok(bytes
            .as_chunks::<16>()
            .0
            .iter()
            .map(|p| {
                std::array::from_fn(|i| f32::from_ne_bytes(p[i * 4..i * 4 + 4].try_into().unwrap()))
            })
            .collect())
    }
    pub fn save_png(&self, path: impl AsRef<std::path::Path>) -> Result<()> {
        let pixels = self.read_frame()?;
        let image = image::RgbImage::from_fn(self.width, self.height, |x, y| {
            let pixel = pixels[((self.height - 1 - y) * self.width + x) as usize];
            image::Rgb(std::array::from_fn(|c| {
                (pixel[c].max(0.0).powf(1.0 / 2.2).min(1.0) * 255.0).round() as u8
            }))
        });
        image.save(path)?;
        Ok(())
    }
    pub(crate) fn read_buffer(&self, source: &wgpu::Buffer, size: u64) -> Result<Vec<u8>> {
        let buffer = self.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("readback"),
            size,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let mut encoder = self.device.create_command_encoder(&Default::default());
        encoder.copy_buffer_to_buffer(source, 0, &buffer, 0, size);
        self.queue.submit([encoder.finish()]);
        let (tx, rx) = std::sync::mpsc::channel();
        buffer
            .slice(..)
            .map_async(wgpu::MapMode::Read, move |result| {
                let _ = tx.send(result);
            });
        self.device.poll(wgpu::PollType::wait_indefinitely())?;
        rx.recv()??;
        let result = buffer.slice(..).get_mapped_range().to_vec();
        buffer.unmap();
        Ok(result)
    }
    pub fn check_tree(&self) -> Result<(u32, usize)> {
        let header = self.read_buffer(&self.tree, 4)?;
        let count = u32::from_ne_bytes(header.try_into().unwrap());
        ensure!(
            (u64::from(count) + 1) * 4 <= self.tree.size(),
            "invalid GPU node count"
        );
        let bytes = self.read_buffer(&self.tree, (u64::from(count) + 1) * 4)?;
        let nodes: Vec<u32> = bytes[4..]
            .as_chunks::<4>()
            .0
            .iter()
            .map(|b| u32::from_ne_bytes(*b))
            .collect();
        let mut todo = vec![(0usize, 0u32)];
        let mut seen = std::collections::HashSet::new();
        while let Some((index, level)) = todo.pop() {
            ensure!(
                index < nodes.len() && level <= self.config.world_levels && seen.insert(index),
                "invalid/cyclic octree pointer"
            );
            let node = nodes[index];
            if node > 1 && node & 3 == 1 {
                let ptr = (node >> 2) as usize;
                for i in 0..8 {
                    todo.push((ptr + i, level + 1));
                }
            }
        }
        Ok((count, seen.len()))
    }
    pub fn compact_tree(&mut self) -> Result<()> {
        self.check_tree()?;
        let header = self.read_buffer(&self.tree, 4)?;
        let count = u32::from_ne_bytes(header.try_into().unwrap());
        let bytes = self.read_buffer(&self.tree, (u64::from(count) + 1) * 4)?;
        let nodes: Vec<u32> = bytes[4..]
            .as_chunks::<4>()
            .0
            .iter()
            .map(|b| u32::from_ne_bytes(*b))
            .collect();
        fn copy(src: &[u32], dst: &mut Vec<u32>, from: usize, to: usize) {
            let n = src[from];
            dst[to] = n;
            if n > 1 && n & 3 == 1 {
                let ptr = dst.len();
                dst.resize(ptr + 8, 0);
                dst[to] = (ptr as u32) << 2 | 1;
                for i in 0..8 {
                    copy(src, dst, (n >> 2) as usize + i, ptr + i);
                }
                if dst.len() == ptr + 8
                    && dst[ptr] & 3 == 3
                    && dst[ptr..].iter().all(|&n| n == dst[ptr])
                {
                    dst[to] = dst[ptr];
                    dst.truncate(ptr);
                }
            }
        }
        let mut compact = vec![0];
        copy(&nodes, &mut compact, 0, 0);
        compact.insert(0, compact.len() as u32);
        self.write_tree(&compact)
    }

    /// Upload a packed tree for compaction or controlled GPU validation scenes.
    pub(crate) fn write_tree(&mut self, words: &[u32]) -> Result<()> {
        ensure!(
            !words.is_empty() && words[0] as usize + 1 == words.len(),
            "invalid packed tree header"
        );
        ensure!(
            words.len() as u64 * 4 <= self.tree.size(),
            "tree exceeds GPU allocation"
        );
        self.queue
            .write_buffer(&self.tree, 0, bytemuck::cast_slice(words));
        // Future allocations must see zeroed children before the parent pointer is published.
        let tail = words.len() as u64 * 4;
        if tail < self.tree.size() {
            let mut encoder = self.device.create_command_encoder(&Default::default());
            encoder.clear_buffer(&self.tree, tail, Some(self.tree.size() - tail));
            self.queue.submit([encoder.finish()]);
        }
        self.samples = 0;
        Ok(())
    }
}
