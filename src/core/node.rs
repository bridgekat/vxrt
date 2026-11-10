//! Local terrain classification, splitting, and material compatibility.
use super::terrain::HeightMap;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Region {
    pub origin: [u32; 3],
    pub size: u32,
}
impl Region {
    pub fn child(self, index: usize) -> Self {
        let half = self.size / 2;
        Self {
            origin: std::array::from_fn(|axis| {
                self.origin[axis] + ((index as u32 >> axis) & 1) * half
            }),
            size: half,
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NodeData {
    pub occupancy: u32,
    pub mixed: bool,
}
impl NodeData {
    pub const EMPTY: Self = Self {
        occupancy: 0,
        mixed: false,
    };
    pub const SOLID: Self = Self {
        occupancy: 1,
        mixed: false,
    };
    pub const MIXED: Self = Self {
        occupancy: 1,
        mixed: true,
    };
}
pub fn classify(map: &HeightMap, region: Region) -> NodeData {
    let [min, max] = map.bounds(region.origin[0], region.origin[2], region.size);
    if region.origin[1] >= max {
        NodeData::EMPTY
    } else if region.origin[1] + region.size <= min {
        NodeData::SOLID
    } else {
        NodeData::MIXED
    }
}
pub fn split(map: &HeightMap, region: Region) -> [NodeData; 8] {
    assert!(region.size > 1);
    std::array::from_fn(|i| classify(map, region.child(i)))
}
/// Lossless merge for automatic compaction: all children must be exact and equal.
pub fn try_merge_exact(children: [NodeData; 8]) -> Option<NodeData> {
    let first = children[0];
    (!first.mixed && children.iter().all(|child| *child == first)).then_some(first)
}

/// Summary stored by every branch, independent of the generation source.
/// Mixed children retain uncertainty and use solid occupancy as a coarse preview.
pub fn merge_summarize(children: [NodeData; 8]) -> NodeData {
    try_merge_exact(children).unwrap_or(NodeData::MIXED)
}
