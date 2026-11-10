//! Direction-independent screen-space LOD and bounded CPU residency maintenance.
use super::{
    node::Region,
    tree::{PoolKind, Tree},
};

/// Must match `lod_check` in main.wgsl; see docs/raycast.md.
pub struct Lod {
    camera: [f32; 3],
    footprint_per_distance: f32,
}
impl Lod {
    pub fn new(camera: [f32; 3], height: u32, fov_degrees: f32, quality: f32) -> Self {
        Self {
            camera: camera.map(|v| v.floor() + 0.5),
            footprint_per_distance: 2.0 * (fov_degrees.to_radians() / 2.0).tan()
                / height.max(1) as f32
                / quality,
        }
    }
    /// Retention uses a larger quality factor than rendering to provide hysteresis.
    pub fn needs_detail(&self, region: Region, retention: f32) -> bool {
        let size = region.size as f32;
        let distance = region
            .origin
            .iter()
            .zip(self.camera)
            .map(|(&origin, camera)| {
                let d = origin as f32 + size * 0.5 - camera;
                d * d
            })
            .sum::<f32>()
            .sqrt();
        size * retention > distance * self.footprint_per_distance
    }
}

/// Persistent rolling cursor; no candidate list or access history is required.
#[derive(Default)]
pub struct Residency {
    cursor: usize,
}
#[derive(Clone, Copy, Debug, Default)]
pub struct SweepStats {
    pub checked: usize,
    pub derefined: usize,
    /// Logical child nodes removed; retained subtree roots are not counted.
    pub unloaded: usize,
}
impl Residency {
    /// Each checked slot costs at most eight child queries and one nine-slot release.
    /// Holes count against the budget; never search unboundedly for a live branch.
    pub fn sweep(
        &mut self,
        tree: &mut Tree,
        lod: &Lod,
        max_checks: usize,
        max_unloads: usize,
    ) -> SweepStats {
        let mut stats = SweepStats::default();
        if !tree.can_regenerate() {
            return stats;
        }
        let capacity = tree.pool_capacity(PoolKind::Branch);
        for _ in 0..max_checks.min(capacity) {
            // Only terminal branches are eligible: every unload removes eight
            // children. Stop before inspecting another slot if none can fit.
            if max_unloads - stats.unloaded < 8 {
                break;
            }
            self.cursor %= capacity;
            let index = self.cursor as u32;
            self.cursor += 1;
            stats.checked += 1;
            let Some(node) = tree.branch_owner(index) else {
                continue;
            };
            if lod.needs_detail(tree.region(node).unwrap(), 1.5) {
                continue;
            }
            let children = tree.children(node).unwrap();
            if children.iter().any(|child| tree.children(*child).is_some()) {
                continue;
            }
            if tree.derefine(node) {
                stats.derefined += 1;
                stats.unloaded += children.len();
            }
        }
        stats
    }
}
