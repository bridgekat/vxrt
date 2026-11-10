use vxrt::core::{packed::validate, tree::Tree};

#[test]
fn rejects_malformed_trees() {
    for words in [
        vec![],
        vec![0],
        vec![2, 3],
        vec![1, 0],
        vec![1, 1],
        vec![1, 2],
        vec![1, 11],
        vec![1, 5],
        vec![9, 5, 5, 3, 3, 3, 3, 3, 3, 3],
    ] {
        assert!(validate(&words, 8).is_err(), "accepted {words:?}");
        assert!(Tree::from_packed(&words, 8, 1024).is_err());
    }
    let mut too_deep = vec![17, 5, 37];
    too_deep.resize(18, 3);
    assert!(validate(&too_deep, 1).is_err());
}

#[test]
fn import_discards_unreachable_storage_and_merges_locally() {
    let mut words = vec![10, 5];
    words.extend([7; 8]);
    words.push(3);
    assert_eq!(validate(&words, 1).unwrap(), 9);
    let tree = Tree::from_packed(&words, 1, 1024).unwrap();
    assert_eq!(tree.counts(), [0, 1]);
    assert_eq!(tree.stats().solid_leaves, 1);
    tree.validate().unwrap();
}
#[test]
fn runtime_merge_preserves_mixed_children() {
    let words = [10, 5, 3, 7, 3, 7, 7, 3, 7, 3, 7];
    let tree = Tree::from_packed(&words, 1, 1024).unwrap();
    let root = tree.root();
    let children = tree.branch_words(root >> 4);
    assert_eq!(
        children.map(|word| (word >> 2) & 1),
        [0, 1, 0, 1, 1, 0, 1, 0]
    );
    assert_eq!(tree.stats().empty_leaves, 4);
    assert_eq!(tree.stats().solid_leaves, 4);
    tree.validate().unwrap();
}

#[test]
fn import_merges_multiple_levels_without_merging_mixed_ancestors() {
    let mut words = vec![73, 5];
    words.extend((0..8).map(|i| ((9 + i * 8) << 2) | 1));
    words.extend([7; 64]);
    let uniform = Tree::from_packed(&words, 2, 1024).unwrap();
    assert_eq!(uniform.counts(), [0, 1]);
    assert_eq!(uniform.validate().unwrap(), 1);

    words[10] = 3;
    let mixed = Tree::from_packed(&words, 2, 1024).unwrap();
    assert_eq!(mixed.counts(), [2, 15]);
    assert_eq!(mixed.stats().empty_leaves, 1);
    assert_eq!(mixed.stats().solid_leaves, 14);
    assert_eq!(mixed.validate().unwrap(), 17);
}
