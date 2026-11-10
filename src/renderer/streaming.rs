//! Asynchronous refinement feedback and batched GPU pool updates.
use super::requests::Requests;
use crate::{
    config::Config,
    core::{
        pool::Handle,
        terrain::HeightMap,
        tree::{Refinement, Tree},
    },
};
use anyhow::{Result, ensure};
use std::sync::mpsc::{self, Receiver, TryRecvError};
use wgpu::util::DeviceExt;
/// Activity during one CPU render frame; feedback refers to an earlier GPU frame.
#[derive(Clone, Copy, Debug, Default)]
pub struct StreamingStats {
    pub frame: u64,
    pub feedback_frame: Option<u64>,
    pub requested: usize,
    pub request_entries: usize,
    pub request_dropped: usize,
    pub split: usize,
    pub uploaded_branches: usize,
    pub uploaded_leaves: usize,
    pub budget_deferred: usize,
    pub pool_limited: usize,
    pub stale: usize,
    pub not_needed: usize,
    pub paused_discarded: usize,
}
impl StreamingStats {
    pub fn uploaded_nodes(self) -> usize {
        self.uploaded_branches + self.uploaded_leaves
    }
}
struct Feedback {
    frame: u64,
    buffer: wgpu::Buffer,
    ready: Receiver<Result<(), wgpu::BufferAsyncError>>,
}
pub(super) struct Streaming {
    pub tree: Tree,
    pub branches: wgpu::Buffer,
    pub leaves: wgpu::Buffer,
    pub requests: Requests,
    scatter: wgpu::ComputePipeline,
    feedback: Option<Feedback>,
    readback_spare: Option<wgpu::Buffer>,
    pub limit: usize,
    pub stats: StreamingStats,
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
impl Streaming {
    pub fn new(device: &wgpu::Device, config: &Config) -> Self {
        let max = (device
            .limits()
            .max_storage_buffer_binding_size
            .min(device.limits().max_buffer_size)
            / 32)
            .min(1 << 28) as usize;
        let limit = 1usize << (usize::BITS - 1 - max.leading_zeros());
        let tree = Tree::new(
            HeightMap::new(config.world_levels, config.max_height),
            limit,
        );
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
            tree,
            branches: storage(device, "branches", 16 * 32),
            leaves: storage(device, "leaves", 16 * 16),
            requests: Requests::new(device, config.max_split_requests),
            scatter,
            feedback: None,
            readback_spare: None,
            limit,
            stats: StreamingStats::default(),
        }
    }
    pub fn process(&mut self, device: &wgpu::Device, config: &Config, paused: bool) -> Result<()> {
        self.stats = StreamingStats {
            frame: self.stats.frame + 1,
            ..Default::default()
        };
        if self.feedback.is_some() {
            device.poll(wgpu::PollType::Poll)?;
        }
        let ready = match self.feedback.as_ref().map(|f| f.ready.try_recv()) {
            Some(Ok(result)) => {
                result?;
                true
            }
            Some(Err(TryRecvError::Disconnected)) => {
                anyhow::bail!("feedback callback disconnected")
            }
            _ => false,
        };
        if ready {
            let feedback = self.feedback.take().unwrap();
            self.stats.feedback_frame = Some(feedback.frame);
            {
                let bytes = feedback.buffer.slice(..).get_mapped_range();
                let words = bytes.as_chunks::<4>().0;
                let total = u32::from_ne_bytes(words[0]) as usize;
                self.stats.request_entries = total.min(words.len() - 1);
                self.stats.request_dropped = total - self.stats.request_entries;
                let mut indices: Vec<_> = words[1..1 + self.stats.request_entries]
                    .iter()
                    .map(|word| u32::from_ne_bytes(*word) - 1)
                    .collect();
                indices.sort_unstable();
                indices.dedup();
                let mut requests: Vec<_> =
                    indices.into_iter().map(|index| Handle { index }).collect();
                self.stats.requested = requests.len();
                if paused {
                    self.stats.paused_discarded = requests.len();
                } else {
                    if !requests.is_empty() {
                        let offset = self.tree.counts()[0] % requests.len();
                        requests.rotate_left(offset);
                    }
                    let total = requests.len();
                    for (i, h) in requests.into_iter().enumerate() {
                        if self.stats.split >= config.splits_per_frame as usize {
                            self.stats.budget_deferred = total - i;
                            break;
                        }
                        match self.tree.refine_feedback_result(h) {
                            Refinement::Split => self.stats.split += 1,
                            Refinement::PoolLimited => self.stats.pool_limited += 1,
                            Refinement::Stale => self.stats.stale += 1,
                            Refinement::NotNeeded => self.stats.not_needed += 1,
                        }
                    }
                }
            }
            feedback.buffer.unmap();
            self.readback_spare = Some(feedback.buffer);
            self.tree.finish_feedback();
        }
        if self.stats.split > 0 {
            self.tree.compact();
        }
        Ok(())
    }
    /// Apply unique slot patches before traversal. Growth copies the old GPU allocation.
    pub fn sync(&mut self, device: &wgpu::Device, encoder: &mut wgpu::CommandEncoder) -> bool {
        let capacities = self.tree.capacities();
        let mut dirty = self.tree.take_dirty();
        for (kind, slots) in dirty.iter_mut().enumerate() {
            slots.retain(|index| self.tree.slot_allocated(kind, *index));
        }
        self.stats.uploaded_branches = dirty[0].len();
        self.stats.uploaded_leaves = dirty[1].len();
        let mut resized = false;
        for kind in 0..2 {
            let stride = if kind == 0 { 8 } else { 4 };
            let bytes = (capacities[kind] * stride * 4) as u64;
            let buffer = if kind == 0 {
                &mut self.branches
            } else {
                &mut self.leaves
            };
            if buffer.size() != bytes {
                let grown = storage(device, "resized node pool", bytes);
                encoder.copy_buffer_to_buffer(buffer, 0, &grown, 0, buffer.size().min(bytes));
                *buffer = grown;
                resized = true;
            }
            if dirty[kind].is_empty() {
                continue;
            }
            let mut words = vec![dirty[kind].len() as u32, stride as u32];
            for &index in &dirty[kind] {
                let data = if kind == 0 {
                    self.tree.branch_words(index).to_vec()
                } else {
                    self.tree.leaf_words(index).to_vec()
                };
                words.push(index);
                words.extend(data);
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
        resized
    }
    pub fn capture(
        &mut self,
        device: &wgpu::Device,
        encoder: &mut wgpu::CommandEncoder,
    ) -> Option<(
        wgpu::Buffer,
        mpsc::Sender<Result<(), wgpu::BufferAsyncError>>,
    )> {
        if self.feedback.is_some() {
            return None;
        }
        let packet = self.requests.merge(device, encoder);
        let buffer = self
            .readback_spare
            .take()
            .filter(|buffer| buffer.size() == packet.size())
            .unwrap_or_else(|| {
                device.create_buffer(&wgpu::BufferDescriptor {
                    label: Some("split feedback readback"),
                    size: packet.size(),
                    usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
                    mapped_at_creation: false,
                })
            });
        encoder.copy_buffer_to_buffer(packet, 0, &buffer, 0, buffer.size());
        self.tree.begin_feedback();
        let (tx, rx) = mpsc::channel();
        self.feedback = Some(Feedback {
            frame: self.stats.frame,
            buffer: buffer.clone(),
            ready: rx,
        });
        Some((buffer, tx))
    }
    pub fn replace(&mut self, tree: Tree) {
        self.tree = tree;
        self.feedback = None;
        self.readback_spare = None;
    }
    pub fn feedback_pending(&self) -> bool {
        self.feedback.is_some()
    }
    pub fn verify(&self, branches: &[u8], leaves: &[u8]) -> Result<()> {
        for (kind, bytes) in [branches, leaves].into_iter().enumerate() {
            let stride = if kind == 0 { 8 } else { 4 };
            for index in 0..self.tree.capacities()[kind] as u32 {
                let expected = if kind == 0 {
                    self.tree.branch_words(index).to_vec()
                } else {
                    self.tree.leaf_words(index).to_vec()
                };
                // Freed slots need not be cleared; only allocated slots are observable.
                if !self.tree.slot_allocated(kind, index) {
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
