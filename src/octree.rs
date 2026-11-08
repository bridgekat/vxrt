//! Validation and compaction of packed octrees, independent of GPU resources.
use anyhow::{Result, ensure};

/// Validate the header and reachable nodes; return the number of reachable nodes.
/// Unreachable storage is allowed and is discarded by compaction.
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
            node & 3 == 3 || (node > 1 && node & 3 == 1),
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

/// Remove unreachable nodes and merge identical leaf children.
pub fn compact(words: &[u32], world_levels: u32) -> Result<Vec<u32>> {
    validate(words, world_levels)?;
    let nodes = &words[1..];
    fn copy(src: &[u32], dst: &mut Vec<u32>, from: usize, to: usize) {
        let n = src[from];
        dst[to] = n;
        if n > 1 && n & 3 == 1 {
            let ptr = dst.len();
            dst.resize(ptr + 8, 0);
            dst[to] = (ptr as u32) << 2 | 1;
            for i in 0..8 {
                copy(src, dst, (n >> 2) as usize + i, ptr + i);
            }
            if dst.len() == ptr + 8
                && dst[ptr] & 3 == 3
                && dst[ptr..].iter().all(|&n| n == dst[ptr])
            {
                dst[to] = dst[ptr];
                dst.truncate(ptr);
            }
        }
    }
    let mut compact = vec![0];
    copy(nodes, &mut compact, 0, 0);
    compact.insert(0, compact.len() as u32);
    Ok(compact)
}
