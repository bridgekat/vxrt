//! Compare hierarchical GPU list merging with a simple CPU stream filter.
use anyhow::{Result, ensure};
use vxrt::renderer::Renderer;
use wgpu::util::DeviceExt;

pub fn check(r: &Renderer) -> Result<()> {
    let shader = r.device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: None,
        source: wgpu::ShaderSource::Wgsl(vxrt::shaders::REQUESTS.into()),
    });
    let pipeline = r
        .device
        .create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
            label: None,
            layout: None,
            module: &shader,
            entry_point: Some("main"),
            compilation_options: Default::default(),
            cache: None,
        });
    for (length, limit, rotation, empty) in [
        (1, 1, 0, false),
        (65, 7, 64, false),
        (5003, 65, 27, false),
        (65537, 16384, 111, false),
        (1048601, 7, 101, false),
        (5003, 64, 55, true),
    ] {
        let input: Vec<u32> = (0..length)
            .map(|i| if empty || i % 3 == 0 { 0 } else { i % 11 + 1 })
            .collect();
        let expected: Vec<_> = (0..length)
            .map(|i| input[((i + rotation) % length) as usize])
            .filter(|v| *v != 0)
            .collect();
        let mut source = r
            .device
            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: None,
                contents: bytemuck::cast_slice(&input),
                usage: wgpu::BufferUsages::STORAGE,
            });
        let mut encoder = r.device.create_command_encoder(&Default::default());
        let mut count = length;
        let mut capacity = 0u32;
        loop {
            let next_count = count.div_ceil(64);
            let next_capacity = (capacity.max(1) * 64).min(limit);
            let target = r.device.create_buffer(&wgpu::BufferDescriptor {
                label: None,
                size: u64::from(next_count) * u64::from(next_capacity + 1) * 4,
                usage: wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_SRC,
                mapped_at_creation: false,
            });
            let params = r
                .device
                .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                    label: None,
                    contents: bytemuck::cast_slice(&[count, capacity, next_capacity, rotation]),
                    usage: wgpu::BufferUsages::UNIFORM,
                });
            let group = r.device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: None,
                layout: &pipeline.get_bind_group_layout(0),
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
                        resource: target.as_entire_binding(),
                    },
                ],
            });
            {
                let mut pass = encoder.begin_compute_pass(&Default::default());
                pass.set_pipeline(&pipeline);
                pass.set_bind_group(0, &group, &[]);
                pass.dispatch_workgroups(next_count.min(1024), next_count.div_ceil(1024), 1);
            }
            source = target;
            count = next_count;
            capacity = next_capacity;
            if count == 1 {
                break;
            }
        }
        r.queue.submit([encoder.finish()]);
        let bytes = r.read_buffer(&source, source.size())?;
        let words: Vec<_> = bytes
            .as_chunks::<4>()
            .0
            .iter()
            .map(|b| u32::from_ne_bytes(*b))
            .collect();
        ensure!(
            words[0] as usize == expected.len(),
            "merge lost total request count"
        );
        let retained = expected.len().min(limit as usize);
        ensure!(
            words[1..1 + retained] == expected[..retained],
            "merge changed order, duplicates, or truncation"
        );
    }
    Ok(())
}
