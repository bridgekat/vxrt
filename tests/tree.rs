use vxrt::core::{terrain::HeightMap, tree::Tree};
#[test]
fn refinement_preserves_valid_topology() {
    let mut tree = Tree::new(HeightMap::from_heights(8, &[3; 64]), 1024);
    let first = tree.requests()[0];
    assert!(tree.refine(first));

    for _ in 0..3 {
        for h in tree.requests() {
            tree.refine(h);
        }
        tree.validate().unwrap();
    }
    assert!(tree.capacities()[1] > 16);
    assert!(tree.counts()[0] > 1);
}
#[test]
fn pool_limit_keeps_a_valid_coarse_tree() {
    let mut tree = Tree::new(HeightMap::from_heights(8, &[3; 64]), 16);
    for _ in 0..5 {
        for h in tree.requests() {
            tree.refine(h);
        }
        tree.validate().unwrap();
    }
    assert!(!tree.requests().is_empty());
    assert!(tree.counts()[1] <= 16);
}

#[test]
fn pending_feedback_survives_upload_sync_but_not_slot_reuse() {
    let heights: Vec<_> = (0..64).map(|i| if i % 2 == 0 { 0 } else { 8 }).collect();
    let mut tree = Tree::new(HeightMap::from_heights(8, &heights), 1024);
    let old = tree.requests()[0];
    tree.take_dirty();
    tree.begin_feedback();
    tree.refine(old);
    tree.take_dirty();
    tree.refine(tree.requests()[0]);
    tree.take_dirty();
    assert!(
        tree.requests().contains(&old),
        "exercise a reused mixed leaf"
    );
    assert!(!tree.refine_feedback(old));
    tree.finish_feedback();
    tree.begin_feedback();
    assert!(tree.refine_feedback(old));
    tree.finish_feedback();
    tree.validate().unwrap();
}

#[test]
fn unchanged_feedback_remains_valid_across_syncs() {
    let mut tree = Tree::new(HeightMap::from_heights(8, &[3; 64]), 1024);
    let h = tree.requests()[0];
    tree.take_dirty();
    tree.begin_feedback();
    tree.take_dirty();
    tree.take_dirty();
    assert!(tree.refine_feedback(h));
    assert!(!tree.refine_feedback(h));
    tree.finish_feedback();
    tree.validate().unwrap();
}

#[test]
fn stats_account_for_all_slots_and_leaf_types() {
    let mut tree = Tree::new(HeightMap::from_heights(8, &[3; 64]), 1024);
    tree.refine(tree.requests()[0]);
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
