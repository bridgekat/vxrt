//! Construction from the independent packed reference format.
use super::*;
use anyhow::{Result, ensure};

impl Tree {
    /// Import an exact diagnostic scene without a procedural generation source.
    pub fn from_packed(words: &[u32], levels: u32, limit: usize) -> Result<Self> {
        ensure!(
            (16..=1 << 28).contains(&limit),
            "pool limit must fit GPU descriptors"
        );
        super::super::packed::validate(words, levels)?;
        let mut tree = Self::empty(limit);
        fn build(
            tree: &mut Tree,
            words: &[u32],
            index: usize,
            region: Region,
            parent: Option<Parent>,
        ) -> Result<Node> {
            let word = words[index + 1];
            let material = if word & 3 != 3 {
                NodeData::MIXED
            } else if word >> 2 == 0 {
                NodeData::EMPTY
            } else {
                NodeData::SOLID
            };
            let n = tree
                .allocate(Entry {
                    material,
                    region,
                    parent,
                    children: None,
                })
                .ok_or_else(|| anyhow::anyhow!("data pool limit"))?;
            if word & 3 != 3 {
                let block = tree
                    .branches
                    .allocate(Branch {
                        children: [n; 8],
                        owner: n,
                    })
                    .ok_or_else(|| anyhow::anyhow!("branch pool limit"))?;
                tree.data.get_mut(n).unwrap().children = Some(block);
                *tree.leaf_counts.counter(material) -= 1;
                let mut children = [n; 8];
                for (i, c) in children.iter_mut().enumerate() {
                    *c = build(
                        tree,
                        words,
                        (word >> 2) as usize + i,
                        region.child(i),
                        Some(Parent { node: n, child: i }),
                    )?;
                }
                tree.branches.get_mut(block).unwrap().children = children;
                tree.dirty_branches.insert(block.index);
                tree.normalize(n);
            }
            Ok(n)
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
