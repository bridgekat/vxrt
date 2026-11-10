//! Asynchronous refinement feedback and batched GPU pool updates.
use super::{requests::Requests, uploads::GpuPools};
use crate::{
    config::Config,
    core::{
        pool::Handle,
        terrain::HeightMap,
        tree::{BRANCH_WORDS, Refinement, Tree},
    },
};
use anyhow::Result;
use std::sync::mpsc::{self, Receiver, TryRecvError};
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
    pub uploaded_data: usize,
    pub updated_nodes: usize,
    pub budget_deferred: usize,
    pub pool_limited: usize,
    pub stale: usize,
    pub not_needed: usize,
    pub paused_discarded: usize,
}
impl StreamingStats {
    pub fn uploaded_nodes(self) -> usize {
        self.updated_nodes
    }
}
struct Feedback {
    frame: u64,
    buffer: wgpu::Buffer,
    ready: Receiver<Result<(), wgpu::BufferAsyncError>>,
}
pub(super) struct Streaming {
    pub tree: Tree,
    pub gpu: GpuPools,
    pub requests: Requests,
    feedback: Option<Feedback>,
    readback_spare: Option<wgpu::Buffer>,
    pub limit: usize,
    pub stats: StreamingStats,
}
impl Streaming {
    pub fn new(device: &wgpu::Device, config: &Config) -> Self {
        let max = (device
            .limits()
            .max_storage_buffer_binding_size
            .min(device.limits().max_buffer_size)
            / (BRANCH_WORDS * 4) as u64)
            .min(1 << 28) as usize;
        let limit = 1usize << (usize::BITS - 1 - max.leading_zeros());
        let tree = Tree::new(
            HeightMap::new(config.world_levels, config.max_height),
            limit,
        );
        Self {
            tree,
            gpu: GpuPools::new(device),
            requests: Requests::new(device, config.max_split_requests),
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
                        let offset = self.tree.stats().branches % requests.len();
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
        Ok(())
    }
    /// Upload the final live records before submitting traversal.
    pub fn sync(&mut self, device: &wgpu::Device, encoder: &mut wgpu::CommandEncoder) -> bool {
        let uploaded = self.gpu.sync(&mut self.tree, device, encoder);
        self.stats.uploaded_branches = uploaded.branches;
        self.stats.uploaded_data = uploaded.data;
        self.stats.updated_nodes = uploaded.nodes;
        uploaded.resized
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
}
