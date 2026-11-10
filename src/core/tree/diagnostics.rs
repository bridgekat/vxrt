//! Explicit full-tree checks; never part of per-frame statistics.
use super::*;
use anyhow::{Result, ensure};
use std::collections::HashSet;

impl Tree {
    /// Explicit full-tree diagnostic for reachability, consistency, compactness and counters.
    pub fn validate(&self) -> Result<usize> {
        let mut todo = vec![(self.root, None)];
        let mut seen = HashSet::new();
        let mut blocks = HashSet::new();
        let mut counts = LeafCounts::default();
        while let Some((n, parent)) = todo.pop() {
            ensure!(seen.insert(n.index), "repeated tree reference");
            let e = self
                .data
                .get(n)
                .ok_or_else(|| anyhow::anyhow!("stale data"))?;
            ensure!(
                e.material.occupancy <= 1
                    && (!e.material.mixed || (e.region.size > 1 && e.material.occupancy == 1)),
                "invalid node data"
            );
            match (parent, e.parent) {
                (None, None) => {}
                (Some((h, i, r)), Some(p)) => ensure!(
                    p.node == h && p.child == i && r == e.region,
                    "invalid parent or region"
                ),
                _ => anyhow::bail!("invalid parent"),
            }
            if let Some(block) = e.children {
                ensure!(blocks.insert(block.index), "repeated child block");
                let b = self
                    .branches
                    .get(block)
                    .ok_or_else(|| anyhow::anyhow!("stale branch"))?;
                ensure!(
                    b.owner == n && e.region.size > 1,
                    "invalid branch owner or size"
                );
                let mut materials = [NodeData::EMPTY; 8];
                for (material, child) in materials.iter_mut().zip(b.children) {
                    *material = self
                        .node_data(child)
                        .ok_or_else(|| anyhow::anyhow!("stale child"))?;
                }
                ensure!(
                    e.material == node::merge_summarize(materials),
                    "inconsistent branch summary"
                );
                ensure!(
                    node::try_merge_exact(materials).is_none(),
                    "noncompact tree: compatible children"
                );
                for (i, c) in b.children.iter().enumerate() {
                    todo.push((*c, Some((n, i, e.region.child(i)))));
                }
            } else {
                *counts.counter(e.material) += 1;
            }
        }
        ensure!(
            seen.len() == self.data.len() && blocks.len() == self.branches.len(),
            "unreachable allocation"
        );
        ensure!(
            counts == self.leaf_counts,
            "leaf counters disagree with resident topology"
        );
        Ok(seen.len())
    }
}
