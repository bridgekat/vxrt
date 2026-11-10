//! Per-ray request slots and a bounded, hierarchical GPU merge.
use anyhow::{Result, ensure};
use wgpu::util::DeviceExt;

pub(super) struct Requests {
    pub pixels: wgpu::Buffer,
    pipeline: wgpu::ComputePipeline,
    levels: Vec<Level>,
    capacity: u32,
    count: u32,
    rotation: u32,
}
struct Level {
    buffer: wgpu::Buffer,
    count: u32,
    capacity: u32,
}
fn buffer(device: &wgpu::Device, words: u64) -> Result<wgpu::Buffer> {
    let size = words * 4;
    ensure!(
        size <= device.limits().max_storage_buffer_binding_size
            && size <= device.limits().max_buffer_size,
        "request buffers exceed adapter limits"
    );
    Ok(device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("split request lists"),
        size,
        usage: wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_SRC,
        mapped_at_creation: false,
    }))
}
impl Requests {
    pub fn new(device: &wgpu::Device, capacity: u32) -> Self {
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("request list merge"),
            source: wgpu::ShaderSource::Wgsl(crate::shaders::REQUESTS.into()),
        });
        Self {
            pixels: buffer(device, 1).unwrap(),
            pipeline: device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
                label: Some("request list merge"),
                layout: None,
                module: &shader,
                entry_point: Some("main"),
                compilation_options: Default::default(),
                cache: None,
            }),
            levels: Vec::new(),
            capacity,
            count: 0,
            rotation: 0,
        }
    }
    pub fn resize(&mut self, device: &wgpu::Device, width: u32, height: u32) -> Result<()> {
        let count = u64::from(width) * u64::from(height)
            + u64::from(width.div_ceil(4) + 1) * u64::from(height.div_ceil(4) + 1);
        ensure!(count <= u64::from(u32::MAX) / 2, "too many request slots");
        let pixels = buffer(device, count)?;
        let mut levels = Vec::new();
        let mut n = count as u32;
        let mut capacity = 1u32;
        loop {
            n = n.div_ceil(64);
            capacity = (capacity * 64).min(self.capacity);
            levels.push(Level {
                buffer: buffer(device, u64::from(n) * (u64::from(capacity) + 1))?,
                count: n,
                capacity,
            });
            if n == 1 {
                break;
            }
        }
        self.pixels = pixels;
        self.levels = levels;
        self.count = count as u32;
        self.rotation = 0;
        Ok(())
    }
    pub fn merge(
        &mut self,
        device: &wgpu::Device,
        encoder: &mut wgpu::CommandEncoder,
    ) -> &wgpu::Buffer {
        let mut source = &self.pixels;
        let mut count = self.count;
        let mut capacity = 0;
        for level in &self.levels {
            let params = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("request merge parameters"),
                contents: bytemuck::cast_slice(&[count, capacity, level.capacity, self.rotation]),
                usage: wgpu::BufferUsages::UNIFORM,
            });
            let group = device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("request merge"),
                layout: &self.pipeline.get_bind_group_layout(0),
                entries: &[
                    wgpu::BindGroupEntry {
                        binding: 0,
                        resource: params.as_entire_binding(),
                    },
                    wgpu::BindGroupEntry {
                        binding: 1,
                        resource: source.as_entire_binding(),
                    },
                    wgpu::BindGroupEntry {
                        binding: 2,
                        resource: level.buffer.as_entire_binding(),
                    },
                ],
            });
            let mut pass = encoder.begin_compute_pass(&Default::default());
            pass.set_pipeline(&self.pipeline);
            pass.set_bind_group(0, &group, &[]);
            pass.dispatch_workgroups(level.count.min(1024), level.count.div_ceil(1024), 1);
            source = &level.buffer;
            count = level.count;
            capacity = level.capacity;
        }
        self.rotation = (self.rotation + self.capacity) % self.count;
        source
    }
}
