//! CPU-owned SVO topology, residency, and dirty slot tracking.
use super::{
    node::{self, Leaf, Region},
    pool::{Handle, Pool},
    terrain::HeightMap,
};
use anyhow::{Result, ensure};
use std::collections::{BTreeSet, HashSet};
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Node {
    Branch(Handle),
    Leaf(Handle),
}
impl Node {
    fn identity(self) -> u32 {
        match self {
            Self::Branch(h) => h.index << 2 | 1,
            Self::Leaf(h) => h.index << 2 | 3,
        }
    }
}
#[derive(Clone, Copy)]
struct Parent {
    branch: Handle,
    child: usize,
}
struct LeafEntry {
    material: Leaf,
    region: Region,
    parent: Option<Parent>,
}
struct Branch {
    children: [Node; 8],
    region: Region,
    parent: Option<Parent>,
}
/// CPU-side resident topology and GPU pool storage; excludes staging and CPU metadata.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TreeStats {
    pub branches: usize,
    pub leaves: usize,
    pub branch_capacity: usize,
    pub leaf_capacity: usize,
    pub empty_leaves: usize,
    pub solid_leaves: usize,
    pub deferred_leaves: usize,
    pub height_map_bytes: usize,
}
impl TreeStats {
    pub fn gpu_pool_bytes(self) -> usize {
        self.branch_capacity * 32 + self.leaf_capacity * 16
    }
    pub fn gpu_live_bytes(self) -> usize {
        self.branches * 32 + self.leaves * 16
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Refinement {
    Split,
    Stale,
    NotNeeded,
    PoolLimited,
}
pub struct Tree {
    branches: Pool<Branch>,
    leaves: Pool<LeafEntry>,
    root: Node,
    map: Option<HeightMap>,
    dirty_branches: BTreeSet<u32>,
    dirty_leaves: BTreeSet<u32>,
}
impl Tree {
    pub fn new(map: HeightMap, limit: usize) -> Self {
        let region = Region {
            origin: [0; 3],
            size: map.side(),
        };
        let material = node::classify(&map, region);
        let mut leaves = Pool::new(16, limit);
        let root = Node::Leaf(
            leaves
                .allocate(LeafEntry {
                    material,
                    region,
                    parent: None,
                })
                .unwrap(),
        );
        Self {
            branches: Pool::new(16, limit),
            leaves,
            root,
            map: Some(map),
            dirty_branches: BTreeSet::new(),
            dirty_leaves: BTreeSet::from([0]),
        }
    }
    pub fn root(&self) -> u32 {
        self.descriptor(self.root)
    }
    /// Cache traversal flags beside the pointer, avoiding a dependent leaf-buffer read.
    fn descriptor(&self, node: Node) -> u32 {
        match node {
            Node::Branch(h) => h.index << 4 | 1,
            Node::Leaf(h) => {
                let material = self.leaves.get(h).unwrap().material;
                h.index << 4 | u32::from(material.mixed) << 3 | material.occupancy << 2 | 3
            }
        }
    }
    pub fn capacities(&self) -> [usize; 2] {
        [self.branches.capacity(), self.leaves.capacity()]
    }
    pub fn stats(&self) -> TreeStats {
        let mut stats = TreeStats {
            branches: self.branches.len(),
            leaves: self.leaves.len(),
            branch_capacity: self.branches.capacity(),
            leaf_capacity: self.leaves.capacity(),
            height_map_bytes: self.map.as_ref().map_or(0, HeightMap::bytes),
            ..Default::default()
        };
        for (_, leaf) in self.leaves.iter() {
            if leaf.material.mixed {
                stats.deferred_leaves += 1;
            } else if leaf.material.occupancy == 0 {
                stats.empty_leaves += 1;
            } else {
                stats.solid_leaves += 1;
            }
        }
        stats
    }
    pub fn counts(&self) -> [usize; 2] {
        [self.branches.len(), self.leaves.len()]
    }
    pub fn requests(&self) -> Vec<Handle> {
        self.leaves
            .iter()
            .filter(|(_, l)| l.material.mixed)
            .map(|(h, _)| h)
            .collect()
    }
    pub fn branch_words(&self, index: u32) -> [u32; 8] {
        self.branches
            .at(index)
            .map(|(_, b)| b.children.map(|node| self.descriptor(node)))
            .unwrap_or([0; 8])
    }
    pub fn leaf_words(&self, index: u32) -> [u32; 4] {
        self.leaves
            .at(index)
            .map(|(_, l)| [l.material.occupancy, u32::from(l.material.mixed), 0, 0])
            .unwrap_or([0; 4])
    }
    pub fn slot_allocated(&self, kind: usize, index: u32) -> bool {
        match kind {
            0 => self.branches.at(index).is_some(),
            1 => self.leaves.at(index).is_some(),
            _ => false,
        }
    }
    /// Anchor invalidation logs to the tree submitted for the next feedback capture.
    pub fn begin_feedback(&mut self) {
        assert!(
            self.dirty_branches.is_empty() && self.dirty_leaves.is_empty(),
            "sync tree before capturing feedback"
        );
        self.branches.begin_feedback();
        self.leaves.begin_feedback();
    }
    pub fn finish_feedback(&mut self) {
        self.branches.finish_feedback();
        self.leaves.finish_feedback();
    }
    /// Apply a request only to the allocation visible to its originating GPU batch.
    pub fn refine_feedback(&mut self, request: Handle) -> bool {
        self.refine_feedback_result(request) == Refinement::Split
    }
    pub fn refine_feedback_result(&mut self, request: Handle) -> Refinement {
        if !self.leaves.accepts_feedback(request) {
            return Refinement::Stale;
        }
        self.refine_result(request)
    }
    pub fn take_dirty(&mut self) -> [BTreeSet<u32>; 2] {
        [
            std::mem::take(&mut self.dirty_branches),
            std::mem::take(&mut self.dirty_leaves),
        ]
    }
    fn replace(&mut self, parent: Option<Parent>, node: Node) {
        if let Some(p) = parent {
            self.branches.get_mut(p.branch).unwrap().children[p.child] = node;
            self.dirty_branches.insert(p.branch.index);
        } else {
            self.root = node;
        }
    }
    pub fn refine(&mut self, request: Handle) -> bool {
        self.refine_result(request) == Refinement::Split
    }
    fn refine_result(&mut self, request: Handle) -> Refinement {
        let Some(map) = &self.map else {
            return Refinement::NotNeeded;
        };
        let Some(leaf) = self.leaves.get(request) else {
            return Refinement::Stale;
        };
        if !leaf.material.mixed || leaf.region.size <= 1 {
            return Refinement::NotNeeded;
        }
        if !self.branches.can_allocate(1) || !self.leaves.can_allocate(8) {
            return Refinement::PoolLimited;
        }
        let region = leaf.region;
        let parent = leaf.parent;
        let materials = node::split(map, region);
        let branch = self
            .branches
            .allocate(Branch {
                children: [Node::Leaf(request); 8],
                region,
                parent,
            })
            .unwrap();
        let children = std::array::from_fn(|i| {
            let h = self
                .leaves
                .allocate(LeafEntry {
                    material: materials[i],
                    region: region.child(i),
                    parent: Some(Parent { branch, child: i }),
                })
                .unwrap();
            self.dirty_leaves.insert(h.index);
            Node::Leaf(h)
        });
        self.branches.get_mut(branch).unwrap().children = children;
        self.dirty_branches.insert(branch.index);
        self.replace(parent, Node::Branch(branch));
        self.leaves.remove(request);
        Refinement::Split
    }
    fn release(&mut self, node: Node) {
        match node {
            Node::Leaf(h) => {
                self.leaves.remove(h);
            }
            Node::Branch(h) => {
                let b = self.branches.remove(h).unwrap();
                for c in b.children {
                    self.release(c);
                }
            }
        }
    }
    fn collapse(&mut self, h: Handle, material: Leaf) {
        let b = self.branches.get(h).unwrap();
        let parent = b.parent;
        let region = b.region;
        // Free descendants before allocating the replacement: no growth is needed.
        self.release(Node::Branch(h));
        let leaf = self
            .leaves
            .allocate(LeafEntry {
                material,
                region,
                parent,
            })
            .unwrap();
        self.dirty_leaves.insert(leaf.index);
        self.replace(parent, Node::Leaf(leaf));
    }
    pub fn compact(&mut self) {
        fn visit(tree: &mut Tree, node: Node) {
            let Node::Branch(h) = node else { return };
            let children = tree.branches.get(h).unwrap().children;
            for c in children {
                visit(tree, c);
            }
            let children = tree.branches.get(h).unwrap().children;
            let mut materials = [Leaf::EMPTY; 8];
            for (i, c) in children.into_iter().enumerate() {
                let Node::Leaf(l) = c else { return };
                materials[i] = tree.leaves.get(l).unwrap().material;
            }
            if let Some(material) = node::merge(materials) {
                tree.collapse(h, material);
            }
        }
        visit(self, self.root);
    }
    pub fn validate(&self) -> Result<usize> {
        let mut todo = vec![(self.root, None)];
        let mut seen = HashSet::new();
        while let Some((n, parent)) = todo.pop() {
            ensure!(seen.insert(n.identity()), "repeated tree reference");
            let (region, actual_parent) = match n {
                Node::Leaf(h) => {
                    let l = self
                        .leaves
                        .get(h)
                        .ok_or_else(|| anyhow::anyhow!("stale leaf"))?;
                    ensure!(
                        l.material.occupancy <= 1 && (!l.material.mixed || l.region.size > 1),
                        "invalid leaf"
                    );
                    (l.region, l.parent)
                }
                Node::Branch(h) => {
                    let b = self
                        .branches
                        .get(h)
                        .ok_or_else(|| anyhow::anyhow!("stale branch"))?;
                    ensure!(b.region.size > 1, "branch below voxel size");
                    for (i, c) in b.children.iter().enumerate() {
                        todo.push((*c, Some((h, i, b.region.child(i)))));
                    }
                    (b.region, b.parent)
                }
            };
            match (parent, actual_parent) {
                (None, None) => {}
                (Some((h, i, r)), Some(p)) => ensure!(
                    p.branch == h && p.child == i && r == region,
                    "invalid parent or region"
                ),
                _ => anyhow::bail!("invalid parent"),
            }
        }
        ensure!(
            seen.len() == self.branches.len() + self.leaves.len(),
            "unreachable allocation"
        );
        Ok(seen.len())
    }
    /// Import an exact packed tree, disabling procedural residency for this scene.
    pub fn from_packed(words: &[u32], levels: u32, limit: usize) -> Result<Self> {
        super::packed::validate(words, levels)?;
        // Allocate only the imported scene; no procedural map or temporary root is needed.
        let mut tree = Self {
            branches: Pool::new(16, limit),
            leaves: Pool::new(16, limit),
            root: Node::Leaf(Handle { index: 0 }),
            map: None,
            dirty_branches: BTreeSet::new(),
            dirty_leaves: BTreeSet::new(),
        };
        fn build(
            tree: &mut Tree,
            words: &[u32],
            index: usize,
            region: Region,
            parent: Option<Parent>,
        ) -> Result<Node> {
            let n = words[index + 1];
            if n & 3 == 3 {
                let h = tree
                    .leaves
                    .allocate(LeafEntry {
                        material: if n >> 2 == 0 {
                            Leaf::EMPTY
                        } else {
                            Leaf::SOLID
                        },
                        region,
                        parent,
                    })
                    .ok_or_else(|| anyhow::anyhow!("leaf pool limit"))?;
                tree.dirty_leaves.insert(h.index);
                Ok(Node::Leaf(h))
            } else {
                let h = tree
                    .branches
                    .allocate(Branch {
                        children: [tree.root; 8],
                        region,
                        parent,
                    })
                    .ok_or_else(|| anyhow::anyhow!("branch pool limit"))?;
                let mut children = [tree.root; 8];
                for (i, c) in children.iter_mut().enumerate() {
                    *c = build(
                        tree,
                        words,
                        (n >> 2) as usize + i,
                        region.child(i),
                        Some(Parent {
                            branch: h,
                            child: i,
                        }),
                    )?;
                }
                tree.branches.get_mut(h).unwrap().children = children;
                tree.dirty_branches.insert(h.index);
                Ok(Node::Branch(h))
            }
        }
        tree.root = build(
            &mut tree,
            words,
            0,
            Region {
                origin: [0; 3],
                size: 1 << levels,
            },
            None,
        )?;
        tree.validate()?;
        Ok(tree)
    }
}
