use vxrt::octree::{compact, validate};

#[test]
fn rejects_malformed_trees() {
    for words in [
        vec![],
        vec![0],
        vec![2, 3],
        vec![1, 0],
        vec![1, 1],
        vec![1, 2],
        vec![1, 5],
        vec![9, 5, 5, 3, 3, 3, 3, 3, 3, 3],
    ] {
        assert!(validate(&words, 8).is_err(), "accepted {words:?}");
        assert!(compact(&words, 8).is_err());
    }
    let mut too_deep = vec![17, 5, 37];
    too_deep.resize(18, 3);
    assert!(validate(&too_deep, 1).is_err());
}

#[test]
fn compaction_removes_unreachable_storage_and_merges_leaves() {
    let mut words = vec![10, 5];
    words.extend([7; 8]);
    words.push(3);
    assert_eq!(validate(&words, 1).unwrap(), 9);
    assert_eq!(compact(&words, 1).unwrap(), [1, 7]);
}

#[test]
fn compaction_preserves_mixed_children_and_is_idempotent() {
    let words = [10, 5, 3, 7, 3, 7, 7, 3, 7, 3, 7];
    let result = compact(&words, 1).unwrap();
    assert_eq!(result, [9, 5, 3, 7, 3, 7, 7, 3, 7, 3]);
    assert_eq!(compact(&result, 1).unwrap(), result);
}
