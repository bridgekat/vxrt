use vxrt::core::pool::Pool;
#[test]
fn doubles_and_reuses_slots_without_moving_live_allocations() {
    let mut pool = Pool::new(2, 8);
    let a = pool.allocate(10).unwrap();
    let b = pool.allocate(20).unwrap();
    let c = pool.allocate(30).unwrap();
    assert_eq!(pool.capacity(), 4);
    assert_eq!(pool.get(a), Some(&10));
    assert_eq!(pool.remove(b), Some(20));
    assert!(pool.remove(b).is_none());
    let d = pool.allocate(40).unwrap();
    assert_eq!(b, d);
    assert_eq!(pool.get(c), Some(&30));
    while pool.allocate(0).is_some() {}
    assert_eq!(pool.capacity(), 8);
    assert_eq!(pool.len(), 8);
}
#[test]
fn feedback_tracks_allocation_identity_until_consumed_or_discarded() {
    let mut pool = Pool::new(2, 8);
    let stable = pool.allocate(10).unwrap();
    let old = pool.allocate(20).unwrap();
    pool.begin_feedback();
    assert!(pool.accepts_feedback(old));
    pool.remove(old);
    assert!(!pool.accepts_feedback(old));
    let reused = pool.allocate(30).unwrap();
    assert_eq!(reused, old);
    assert!(!pool.accepts_feedback(reused));
    let new = pool.allocate(40).unwrap();
    assert!(!pool.accepts_feedback(new));
    assert!(pool.accepts_feedback(stable));
    pool.finish_feedback();
    assert!(!pool.accepts_feedback(stable));
    pool.begin_feedback();
    assert!(pool.accepts_feedback(reused));
    assert!(pool.accepts_feedback(new));
    pool.finish_feedback();
}
