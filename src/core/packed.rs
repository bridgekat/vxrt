//! Packed scene interchange: a node-count header followed by tagged words.
//! Branch words point to eight contiguous children; leaves contain binary occupancy.
//! This format is imported into the runtime pools, never traversed by the GPU directly.
use super::terrain::height;
use anyhow::{Result, ensure};

/// Validate the header and reachable nodes; return the number of reachable nodes.
/// Unreachable storage is allowed and ignored during import.
pub fn validate(words: &[u32], world_levels: u32) -> Result<usize> {
    ensure!(
        (1..=10).contains(&world_levels),
        "world_levels must be 1..=10"
    );
    ensure!(
        words.len() >= 2 && words[0] as usize == words.len() - 1,
        "invalid packed tree header"
    );
    let nodes = &words[1..];
    let mut todo = vec![(0usize, 0u32)];
    let mut seen = std::collections::HashSet::new();
    while let Some((index, level)) = todo.pop() {
        ensure!(
            index < nodes.len() && level <= world_levels && seen.insert(index),
            "invalid/cyclic octree pointer"
        );
        let node = nodes[index];
        ensure!(
            (node & 3 == 3 && node >> 2 <= 1) || (node > 1 && node & 3 == 1),
            "invalid tree node"
        );
        if node > 1 && node & 3 == 1 {
            let ptr = (node >> 2) as usize;
            for i in 0..8 {
                todo.push((ptr + i, level + 1));
            }
        }
    }
    Ok(seen.len())
}

/// Build an exact reference scene independently of streaming and min/max classification.
pub fn generate_terrain(levels: u32, max_height: u32) -> Vec<u32> {
    assert!((1..=10).contains(&levels));
    let size = 1usize << levels;
    let heights: Vec<i64> = (0..size * size)
        .map(|i| height((i / size) as f64, (i % size) as f64))
        .collect();
    fn node(
        nodes: &mut Vec<u32>,
        heights: &[i64],
        world: usize,
        max_height: usize,
        index: usize,
        p: [usize; 3],
        size: usize,
    ) {
        if p[1] >= max_height {
            nodes[index] = 3;
            return;
        }
        if size == 1 {
            nodes[index] = 3 | (u32::from((p[1] as i64) < heights[p[0] * world + p[2]]) << 2);
            return;
        }
        let ptr = nodes.len();
        nodes[index] = (ptr as u32) << 2 | 1;
        nodes.resize(ptr + 8, 0);
        let half = size / 2;
        for i in 0..8 {
            node(
                nodes,
                heights,
                world,
                max_height,
                ptr + i,
                [
                    p[0] + (i & 1) * half,
                    p[1] + ((i >> 1) & 1) * half,
                    p[2] + ((i >> 2) & 1) * half,
                ],
                half,
            );
        }
        if nodes.len() == ptr + 8 && nodes[ptr..].iter().all(|&n| n == nodes[ptr]) {
            nodes[index] = nodes[ptr];
            nodes.truncate(ptr);
        }
    }
    let mut nodes = vec![0];
    node(
        &mut nodes,
        &heights,
        size,
        max_height as usize,
        0,
        [0; 3],
        size,
    );
    nodes.insert(0, nodes.len() as u32);
    nodes
}
