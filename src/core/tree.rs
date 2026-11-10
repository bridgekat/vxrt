//! CPU-owned topology with consistent summaries and local exact compaction.
use super::{
    node::{self, NodeData, Region},
    pool::{Handle, Pool},
    terrain::HeightMap,
};
use std::collections::BTreeSet;

mod diagnostics;
mod import;
mod storage;
pub use storage::{BRANCH_WORDS, DATA_WORDS, PoolKind};
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TreeStats {
    pub branches: usize,
    pub leaves: usize,
    pub data_nodes: usize,
    pub branch_capacity: usize,
    pub data_capacity: usize,
    pub empty_leaves: usize,
    pub solid_leaves: usize,
    pub deferred_leaves: usize,
    pub height_map_bytes: usize,
}
impl TreeStats {
    pub fn gpu_pool_bytes(self) -> usize {
        (self.branch_capacity * BRANCH_WORDS + self.data_capacity * DATA_WORDS) * 4
    }
    pub fn gpu_live_bytes(self) -> usize {
        (self.branches * BRANCH_WORDS + self.data_nodes * DATA_WORDS) * 4
    }
}

/// A data-slot allocation; stable across refinement and derefinement, not slot reuse.
pub type Node = Handle;
#[derive(Clone, Copy)]
struct Parent {
    node: Node,
    child: usize,
}
struct Entry {
    material: NodeData,
    region: Region,
    parent: Option<Parent>,
    children: Option<Handle>,
}
struct Branch {
    children: [Node; 8],
    owner: Node,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Refinement {
    Split,
    Stale,
    NotNeeded,
    PoolLimited,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
struct LeafCounts {
    empty: usize,
    solid: usize,
    deferred: usize,
}
impl LeafCounts {
    fn counter(&mut self, material: NodeData) -> &mut usize {
        if material.mixed {
            &mut self.deferred
        } else if material.occupancy == 0 {
            &mut self.empty
        } else {
            &mut self.solid
        }
    }
}
pub struct Tree {
    branches: Pool<Branch>,
    data: Pool<Entry>,
    root: Node,
    map: Option<HeightMap>,
    dirty_branches: BTreeSet<u32>,
    dirty_data: BTreeSet<u32>,
    leaf_counts: LeafCounts,
    height_map_bytes: usize,
}
impl Tree {
    fn empty(limit: usize) -> Self {
        assert!(
            (16..=1 << 28).contains(&limit),
            "pool limit must fit GPU descriptors"
        );
        Self {
            branches: Pool::new(16, limit),
            data: Pool::new(16, limit),
            root: Handle { index: 0 },
            map: None,
            dirty_branches: BTreeSet::new(),
            dirty_data: BTreeSet::new(),
            leaf_counts: LeafCounts::default(),
            height_map_bytes: 0,
        }
    }
    pub fn new(map: HeightMap, limit: usize) -> Self {
        let region = Region {
            origin: [0; 3],
            size: map.side(),
        };
        let material = node::classify(&map, region);
        let mut tree = Self::empty(limit);
        tree.height_map_bytes = map.bytes();
        tree.map = Some(map);
        tree.root = tree
            .allocate(Entry {
                material,
                region,
                parent: None,
                children: None,
            })
            .unwrap();
        tree
    }
    pub fn root_node(&self) -> Node {
        self.root
    }
    pub fn region(&self, node: Node) -> Option<Region> {
        self.data.get(node).map(|e| e.region)
    }
    pub fn node_data(&self, node: Node) -> Option<NodeData> {
        self.data.get(node).map(|e| e.material)
    }
    pub fn children(&self, node: Node) -> Option<[Node; 8]> {
        Some(self.branches.get(self.data.get(node)?.children?)?.children)
    }
    pub fn nodes(&self) -> impl Iterator<Item = Node> + '_ {
        self.data.iter().map(|(h, _)| h)
    }
    pub fn stats(&self) -> TreeStats {
        TreeStats {
            branches: self.branches.len(),
            leaves: self.data.len() - self.branches.len(),
            data_nodes: self.data.len(),
            branch_capacity: self.branches.capacity(),
            data_capacity: self.data.capacity(),
            height_map_bytes: self.height_map_bytes,
            empty_leaves: self.leaf_counts.empty,
            solid_leaves: self.leaf_counts.solid,
            deferred_leaves: self.leaf_counts.deferred,
        }
    }
    /// Explicit diagnostic enumeration; normal rendering uses GPU requests.
    pub fn refinable_leaves(&self) -> Vec<Node> {
        self.data
            .iter()
            .filter(|(_, e)| {
                self.map.is_some() && e.children.is_none() && e.material.mixed && e.region.size > 1
            })
            .map(|(h, _)| h)
            .collect()
    }
    fn allocate(&mut self, e: Entry) -> Option<Node> {
        let material = e.material;
        let h = self.data.allocate(e)?;
        *self.leaf_counts.counter(material) += 1;
        self.dirty_data.insert(h.index);
        Some(h)
    }
    fn changed(&mut self, node: Node) {
        self.dirty_data.insert(node.index);
        if let Some(p) = self.data.get(node).unwrap().parent {
            self.dirty_branches
                .insert(self.data.get(p.node).unwrap().children.unwrap().index);
        }
    }
    /// Load one level from the terrain source, preserving summaries and compactness.
    pub fn refine(&mut self, node: Node) -> bool {
        self.refine_result(node) == Refinement::Split
    }
    fn refine_result(&mut self, node: Node) -> Refinement {
        let Some(e) = self.data.get(node) else {
            return Refinement::Stale;
        };
        let Some(map) = &self.map else {
            return Refinement::NotNeeded;
        };
        if e.children.is_some() || !e.material.mixed || e.region.size <= 1 {
            return Refinement::NotNeeded;
        }
        if !self.branches.can_allocate(1) || !self.data.can_allocate(8) {
            return Refinement::PoolLimited;
        }
        let region = e.region;
        let material = e.material;
        let materials = node::split(map, region);
        let block = self
            .branches
            .allocate(Branch {
                children: [node; 8],
                owner: node,
            })
            .unwrap();
        let children = std::array::from_fn(|i| {
            self.allocate(Entry {
                material: materials[i],
                region: region.child(i),
                parent: Some(Parent { node, child: i }),
                children: None,
            })
            .unwrap()
        });
        self.branches.get_mut(block).unwrap().children = children;
        self.data.get_mut(node).unwrap().children = Some(block);
        *self.leaf_counts.counter(material) -= 1;
        self.dirty_branches.insert(block.index);
        self.changed(node);
        self.update_upwards(node);
        Refinement::Split
    }
    /// Retain this node's summary and identity; discard only its descendants.
    /// Imported scenes cannot regenerate discarded detail.
    pub fn derefine(&mut self, node: Node) -> bool {
        let Some(e) = self.data.get(node) else {
            return false;
        };
        if e.children.is_none() {
            return false;
        }
        // The summary is unchanged, so ancestor consistency and compatibility
        // are unchanged too; only the parent descriptor needs a topology patch.
        self.drop_children(node);
        true
    }
    fn drop_children(&mut self, node: Node) {
        let e = self.data.get_mut(node).unwrap();
        let block = e.children.take().unwrap();
        let material = e.material;
        let b = self.branches.remove(block).unwrap();
        self.dirty_branches.remove(&block.index);
        for child in b.children {
            self.release_subtree(child);
        }
        *self.leaf_counts.counter(material) += 1;
        self.changed(node);
    }
    fn release_subtree(&mut self, node: Node) {
        let e = self.data.remove(node).unwrap();
        self.dirty_data.remove(&node.index);
        if let Some(block) = e.children {
            let b = self.branches.remove(block).unwrap();
            self.dirty_branches.remove(&block.index);
            for child in b.children {
                self.release_subtree(child);
            }
        } else {
            *self.leaf_counts.counter(e.material) -= 1;
        }
    }
    /// Restore both invariants at one node whose children are already consistent.
    fn normalize(&mut self, node: Node) {
        if let Some(children) = self.children(node) {
            let materials = children.map(|c| self.node_data(c).unwrap());
            let summary = node::merge_summarize(materials);
            if self.node_data(node).unwrap() != summary {
                self.data.get_mut(node).unwrap().material = summary;
                self.changed(node);
            }
            if node::try_merge_exact(materials).is_some() {
                self.drop_children(node);
            }
        }
    }

    fn update_upwards(&mut self, mut node: Node) {
        loop {
            self.normalize(node);
            let Some(p) = self.data.get(node).unwrap().parent else {
                break;
            };
            node = p.node;
        }
    }
}
