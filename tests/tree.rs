use vxrt::core::{pool::Handle, terrain::HeightMap, tree::Tree};
#[test]
fn refinement_preserves_valid_topology() {
    let mut tree = Tree::new(HeightMap::from_heights(8, &[3; 64]), 1024);
    let first = tree.refinable_leaves()[0];
    assert!(tree.refine(first));

    for _ in 0..3 {
        for h in tree.refinable_leaves() {
            tree.refine(h);
        }
        tree.validate().unwrap();
    }
    assert!(tree.stats().data_capacity > 16);
    assert!(tree.stats().branches > 1);
}
#[test]
fn pool_limit_keeps_a_valid_coarse_tree() {
    let mut tree = Tree::new(HeightMap::from_heights(8, &[3; 64]), 16);
    for _ in 0..5 {
        for h in tree.refinable_leaves() {
            tree.refine(h);
        }
        tree.validate().unwrap();
    }
    assert!(!tree.refinable_leaves().is_empty());
    assert!(tree.stats().leaves <= 16);
}

#[test]
fn stats_account_for_all_slots_and_leaf_types() {
    let mut tree = Tree::new(HeightMap::from_heights(8, &[3; 64]), 1024);
    tree.refine(tree.refinable_leaves()[0]);
    let stats = tree.stats();
    assert_eq!(stats.branches, 1);
    assert_eq!(stats.leaves, 8);
    assert_eq!(
        stats.empty_leaves + stats.solid_leaves + stats.deferred_leaves,
        stats.leaves
    );
    assert_eq!(stats.deferred_leaves, 4);
    assert_eq!(stats.empty_leaves, 4);
    assert!(stats.gpu_pool_bytes() >= stats.gpu_live_bytes());
    assert_eq!(stats.height_map_bytes, (64 + 16 + 4 + 1) * 8);
}

fn refine_all(tree: &mut Tree) {
    while !tree.refinable_leaves().is_empty() {
        for leaf in tree.refinable_leaves() {
            assert!(tree.refine(leaf));
        }
        tree.validate().unwrap();
    }
}

#[test]
fn unload_and_reload_preserve_counts_and_reuse_capacity() {
    let mut tree = Tree::new(HeightMap::from_heights(8, &[3; 64]), 4096);
    refine_all(&mut tree);
    let full = tree.stats();
    let root = tree.root_node();
    assert!(tree.derefine(root));
    assert_eq!([tree.stats().branches, tree.stats().leaves], [0, 1]);
    assert_eq!(tree.stats().deferred_leaves, 1);
    assert_eq!(tree.stats().empty_leaves + tree.stats().solid_leaves, 0);
    assert_eq!(tree.validate().unwrap(), 1);
    assert!(!tree.derefine(root));
    assert!(!tree.derefine(tree.root_node()));
    refine_all(&mut tree);
    assert_eq!(tree.stats(), full);
}

#[test]
fn arbitrary_local_updates_keep_counters_and_compactness() {
    let heights: Vec<_> = (0..64).map(|i| (i * 7 + i / 8) % 9).collect();
    let mut tree = Tree::new(HeightMap::from_heights(8, &heights), 256);
    let mut seed = 0x12345678u32;
    for _ in 0..400 {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        if seed.is_multiple_of(4) {
            let branches: Vec<_> = tree
                .nodes()
                .filter(|n| tree.children(*n).is_some())
                .collect();
            if !branches.is_empty() {
                assert!(tree.derefine(branches[seed as usize % branches.len()]));
            }
        } else {
            let leaves = tree.refinable_leaves();
            if !leaves.is_empty() {
                tree.refine(leaves[seed as usize % leaves.len()]);
            }
        }
        tree.validate().unwrap();
        let mut counts = [0; 3];
        for index in 0..tree.stats().data_capacity as u32 {
            if tree.slot_allocated(vxrt::core::tree::PoolKind::Data, index)
                && tree.data_words(index)[2] == 0
            {
                let [occupied, deferred, ..] = tree.data_words(index);
                counts[if deferred != 0 { 2 } else { occupied as usize }] += 1;
            }
        }
        let stats = tree.stats();
        assert_eq!(
            [
                stats.empty_leaves,
                stats.solid_leaves,
                stats.deferred_leaves
            ],
            counts
        );
        assert_eq!(stats.leaves, counts.iter().sum());
        assert!(!tree.refine(Handle { index: u32::MAX }));
        assert!(!tree.derefine(Handle { index: u32::MAX }));
        assert_eq!(tree.stats(), stats);
    }
}

#[test]
fn imported_scenes_summarize_without_a_generation_source() {
    let mut tree = Tree::from_packed(&[9, 5, 3, 7, 3, 7, 3, 7, 3, 7], 1, 16).unwrap();
    assert!(tree.derefine(tree.root_node()));
    assert_eq!([tree.stats().branches, tree.stats().leaves], [0, 1]);
    assert_eq!(tree.stats().deferred_leaves, 1);
    assert_eq!(tree.stats().empty_leaves + tree.stats().solid_leaves, 0);
    let leaf = tree.root_node();
    assert!(!tree.refine(leaf));
    assert!(tree.refinable_leaves().is_empty());
    tree.validate().unwrap();
}

#[test]
fn all_nodes_retain_consistent_data_across_residency_changes() {
    use vxrt::core::node::merge_summarize;
    let mut tree = Tree::new(HeightMap::from_heights(8, &[3; 64]), 4096);
    let root = tree.root_node();
    let summary = tree.node_data(root);
    refine_all(&mut tree);
    assert_eq!(tree.root_node(), root);
    assert_eq!(tree.node_data(root), summary);
    assert_eq!(
        tree.stats().data_nodes,
        tree.stats().branches + tree.stats().leaves
    );
    for node in tree.nodes() {
        if let Some(children) = tree.children(node) {
            assert_eq!(
                tree.node_data(node),
                Some(merge_summarize(
                    children.map(|c| tree.node_data(c).unwrap())
                ))
            );
            let block = tree.data_words(node.index)[2] - 1;
            assert_eq!(tree.branch_words(block)[8], node.index);
        }
    }
    let child = tree
        .children(root)
        .unwrap()
        .into_iter()
        .find(|c| tree.children(*c).is_some())
        .unwrap();
    let child_summary = tree.node_data(child);
    assert!(tree.derefine(child));
    assert_eq!(tree.node_data(child), child_summary);
    assert_eq!(tree.node_data(root), summary);
    assert_eq!(tree.data_words(child.index)[2], 0);
    tree.validate().unwrap();
    assert!(tree.derefine(root));
    assert_eq!(tree.root_node(), root);
    assert_eq!(tree.node_data(root), summary);
    assert_eq!(tree.stats().data_nodes, 1);
    tree.validate().unwrap();
}
