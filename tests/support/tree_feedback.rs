use crate::core::{
    terrain::HeightMap,
    tree::{Refinement, Tree},
};

#[test]
fn unloading_invalidates_all_descendant_feedback_before_slot_reuse() {
    let mut tree = Tree::new(HeightMap::from_heights(8, &[3; 64]), 1024);
    tree.refine(tree.refinable_leaves()[0]);
    let pending = tree.refinable_leaves();
    tree.take_dirty();
    tree.begin_feedback();
    assert!(tree.derefine(tree.root_node()));
    tree.take_dirty();
    assert!(tree.refine(tree.refinable_leaves()[0]));
    tree.take_dirty();
    for leaf in pending {
        assert_eq!(tree.refine_feedback_result(leaf), Refinement::Stale);
    }
    tree.finish_feedback();
    tree.validate().unwrap();
}
#[test]
fn retained_node_identity_survives_refine_derefine_and_sync() {
    let heights: Vec<_> = (0..64).map(|i| if i % 2 == 0 { 0 } else { 8 }).collect();
    let mut tree = Tree::new(HeightMap::from_heights(8, &heights), 1024);
    let old = tree.refinable_leaves()[0];
    tree.take_dirty();
    tree.begin_feedback();
    tree.refine(old);
    tree.take_dirty();
    tree.refine(tree.refinable_leaves()[0]);
    tree.take_dirty();
    assert_eq!(tree.refine_feedback_result(old), Refinement::NotNeeded);
    assert!(tree.derefine(old));
    tree.take_dirty();
    assert_eq!(tree.root_node(), old);
    tree.finish_feedback();
    tree.begin_feedback();
    assert_eq!(tree.refine_feedback_result(old), Refinement::Split);
    tree.finish_feedback();
    tree.validate().unwrap();
}

#[test]
fn unchanged_feedback_remains_valid_across_syncs() {
    let mut tree = Tree::new(HeightMap::from_heights(8, &[3; 64]), 1024);
    let h = tree.refinable_leaves()[0];
    tree.take_dirty();
    tree.begin_feedback();
    tree.take_dirty();
    tree.take_dirty();
    assert_eq!(tree.refine_feedback_result(h), Refinement::Split);
    assert_ne!(tree.refine_feedback_result(h), Refinement::Split);
    tree.finish_feedback();
    tree.validate().unwrap();
}
