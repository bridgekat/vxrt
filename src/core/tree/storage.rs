//! CPU serialization and snapshot tracking for the GPU-visible tree.
use super::*;

/// Eight cached child descriptors followed by the owning data-slot index.
pub const BRANCH_WORDS: usize = 9;
/// Occupancy, mixed flag, child-block index plus one, and a reserved word.
pub const DATA_WORDS: usize = 4;
/// Identifies a runtime pool without conflating child blocks with node data.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PoolKind {
    Branch,
    Data,
}
impl PoolKind {
    pub(crate) const ALL: [Self; 2] = [Self::Branch, Self::Data];
    pub(crate) fn words(self) -> usize {
        match self {
            Self::Branch => BRANCH_WORDS,
            Self::Data => DATA_WORDS,
        }
    }
}

/// Final live records changed since the last upload; separate from feedback logs.
pub(crate) struct DirtySlots {
    pub branches: BTreeSet<u32>,
    pub data: BTreeSet<u32>,
}
impl DirtySlots {
    pub fn slots(&self, kind: PoolKind) -> &BTreeSet<u32> {
        match kind {
            PoolKind::Branch => &self.branches,
            PoolKind::Data => &self.data,
        }
    }
}

impl Tree {
    pub fn root(&self) -> u32 {
        self.descriptor(self.root)
    }
    fn descriptor(&self, node: Node) -> u32 {
        let e = self.data.get(node).unwrap();
        let flags = u32::from(e.material.mixed) << 3 | e.material.occupancy << 2;
        match e.children {
            Some(b) => b.index << 4 | flags | 1,
            None => node.index << 4 | flags | 3,
        }
    }
    pub fn pool_capacity(&self, kind: PoolKind) -> usize {
        match kind {
            PoolKind::Branch => self.branches.capacity(),
            PoolKind::Data => self.data.capacity(),
        }
    }
    pub fn branch_words(&self, index: u32) -> [u32; BRANCH_WORDS] {
        self.branches
            .at(index)
            .map(|(_, b)| {
                std::array::from_fn(|i| {
                    if i == 8 {
                        b.owner.index
                    } else {
                        self.descriptor(b.children[i])
                    }
                })
            })
            .unwrap_or([0; BRANCH_WORDS])
    }
    pub fn data_words(&self, index: u32) -> [u32; DATA_WORDS] {
        self.data
            .at(index)
            .map(|(_, e)| {
                [
                    e.material.occupancy,
                    u32::from(e.material.mixed),
                    e.children.map_or(0, |h| h.index + 1),
                    0,
                ]
            })
            .unwrap_or([0; DATA_WORDS])
    }
    pub(crate) fn branch_owner(&self, index: u32) -> Option<Node> {
        self.branches.at(index).map(|(_, b)| b.owner)
    }
    pub fn slot_allocated(&self, kind: PoolKind, index: u32) -> bool {
        match kind {
            PoolKind::Branch => self.branches.at(index).is_some(),
            PoolKind::Data => self.data.at(index).is_some(),
        }
    }
    pub(crate) fn begin_feedback(&mut self) {
        assert!(
            self.dirty_branches.is_empty() && self.dirty_data.is_empty(),
            "sync tree before capturing feedback"
        );
        self.data.begin_feedback();
    }
    pub(crate) fn finish_feedback(&mut self) {
        self.data.finish_feedback();
    }
    pub(crate) fn refine_feedback_result(&mut self, request: Node) -> Refinement {
        if !self.data.accepts_feedback(request) {
            return Refinement::Stale;
        }
        self.refine_result(request)
    }
    pub(crate) fn take_dirty(&mut self) -> DirtySlots {
        DirtySlots {
            branches: std::mem::take(&mut self.dirty_branches),
            data: std::mem::take(&mut self.dirty_data),
        }
    }
}
