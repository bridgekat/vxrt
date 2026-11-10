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
pub struct Leaf {
    pub occupancy: u32,
    pub mixed: bool,
}
impl Leaf {
    pub const EMPTY: Self = Self {
        occupancy: 0,
        mixed: false,
    };
    pub const SOLID: Self = Self {
        occupancy: 1,
        mixed: false,
    };
    pub const DEFERRED: Self = Self {
        occupancy: 1,
        mixed: true,
    };
}
pub fn classify(map: &HeightMap, region: Region) -> Leaf {
    let [min, max] = map.bounds(region.origin[0], region.origin[2], region.size);
    if region.origin[1] >= max {
        Leaf::EMPTY
    } else if region.origin[1] + region.size <= min {
        Leaf::SOLID
    } else {
        Leaf::DEFERRED
    }
}
pub fn split(map: &HeightMap, region: Region) -> [Leaf; 8] {
    assert!(region.size > 1);
    std::array::from_fn(|i| classify(map, region.child(i)))
}
pub fn merge(children: [Leaf; 8]) -> Option<Leaf> {
    let first = children[0];
    (!first.mixed && children.iter().all(|child| *child == first)).then_some(first)
}
