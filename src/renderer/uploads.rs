//! GPU pool allocation, scatter uploads, and explicit readback verification.
//! Feedback lifetime and refinement scheduling belong to streaming.
use crate::core::tree::{BRANCH_WORDS, DATA_WORDS, PoolKind, Tree};
use anyhow::{Result, ensure};
use wgpu::util::DeviceExt;

pub(super) struct GpuPools {
    pub branches: wgpu::Buffer,
    pub data: wgpu::Buffer,
    scatter: wgpu::ComputePipeline,
}
pub(super) struct Uploaded {
    pub branches: usize,
    pub data: usize,
    pub nodes: usize,
    pub resized: bool,
}
fn storage(device: &wgpu::Device, label: &str, size: u64) -> wgpu::Buffer {
    device.create_buffer(&wgpu::BufferDescriptor {
        label: Some(label),
        size,
        usage: wgpu::BufferUsages::STORAGE
            | wgpu::BufferUsages::COPY_DST
            | wgpu::BufferUsages::COPY_SRC,
        mapped_at_creation: false,
    })
}

impl GpuPools {
    pub fn new(device: &wgpu::Device) -> Self {
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("scatter uploads"),
            source: wgpu::ShaderSource::Wgsl(crate::shaders::SCATTER.into()),
        });
        let scatter = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
            label: Some("scatter uploads"),
            layout: None,
            module: &shader,
            entry_point: Some("main"),
            compilation_options: Default::default(),
            cache: None,
        });
        Self {
            branches: storage(device, "branches", (16 * BRANCH_WORDS * 4) as u64),
            data: storage(device, "data", (16 * DATA_WORDS * 4) as u64),
            scatter,
        }
    }
    /// Apply unique slot patches before traversal. Growth copies the old GPU allocation.
    pub fn sync(
        &mut self,
        tree: &mut Tree,
        device: &wgpu::Device,
        encoder: &mut wgpu::CommandEncoder,
    ) -> Uploaded {
        let mut dirty = tree.take_dirty();
        dirty
            .branches
            .retain(|index| tree.slot_allocated(PoolKind::Branch, *index));
        dirty
            .data
            .retain(|index| tree.slot_allocated(PoolKind::Data, *index));
        let mut updated = dirty.data.clone();
        updated.extend(
            dirty
                .branches
                .iter()
                .filter_map(|i| tree.branch_owner(*i).map(|h| h.index)),
        );

        let mut resized = false;
        for pool in PoolKind::ALL {
            let slots = dirty.slots(pool);
            let stride = pool.words();
            let bytes = (tree.pool_capacity(pool) * stride * 4) as u64;
            let buffer = match pool {
                PoolKind::Branch => &mut self.branches,
                PoolKind::Data => &mut self.data,
            };
            if buffer.size() != bytes {
                let grown = storage(device, "resized node pool", bytes);
                encoder.copy_buffer_to_buffer(buffer, 0, &grown, 0, buffer.size().min(bytes));
                *buffer = grown;
                resized = true;
            }
            if slots.is_empty() {
                continue;
            }
            let mut words = Vec::with_capacity(2 + slots.len() * (stride + 1));
            words.extend([slots.len() as u32, stride as u32]);
            for &index in slots {
                words.push(index);
                match pool {
                    PoolKind::Branch => words.extend(tree.branch_words(index)),
                    PoolKind::Data => words.extend(tree.data_words(index)),
                }
            }
            let patches = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("scattered node patches"),
                contents: bytemuck::cast_slice(&words),
                usage: wgpu::BufferUsages::STORAGE,
            });
            let group = device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("scatter batch"),
                layout: &self.scatter.get_bind_group_layout(0),
                entries: &[
                    wgpu::BindGroupEntry {
                        binding: 0,
                        resource: buffer.as_entire_binding(),
                    },
                    wgpu::BindGroupEntry {
                        binding: 1,
                        resource: patches.as_entire_binding(),
                    },
                ],
            });
            let mut pass = encoder.begin_compute_pass(&Default::default());
            pass.set_pipeline(&self.scatter);
            pass.set_bind_group(0, &group, &[]);
            let groups = words[0].div_ceil(64);
            pass.dispatch_workgroups(groups.min(1024), groups.div_ceil(1024), 1);
        }
        Uploaded {
            branches: dirty.branches.len(),
            data: dirty.data.len(),
            nodes: updated.len(),
            resized,
        }
    }
    pub fn verify(&self, tree: &Tree, branches: &[u8], data: &[u8]) -> Result<()> {
        for (pool, bytes) in [(PoolKind::Branch, branches), (PoolKind::Data, data)] {
            let stride = pool.words();
            for index in 0..tree.pool_capacity(pool) as u32 {
                let expected = match pool {
                    PoolKind::Branch => tree.branch_words(index).to_vec(),
                    PoolKind::Data => tree.data_words(index).to_vec(),
                };
                // Freed slots need not be cleared; only allocated slots are observable.
                if !tree.slot_allocated(pool, index) {
                    continue;
                }
                for (offset, value) in expected.iter().enumerate() {
                    let start = (index as usize * stride + offset) * 4;
                    ensure!(
                        u32::from_ne_bytes(bytes[start..start + 4].try_into().unwrap()) == *value,
                        "GPU pool diverged from CPU tree"
                    );
                }
            }
        }
        Ok(())
    }
}
