use vxrt::core::{
    node::Region,
    residency::{Lod, Residency},
    terrain::HeightMap,
    tree::Tree,
};
fn loaded() -> Tree {
    let mut tree = Tree::new(HeightMap::from_heights(8, &[3; 64]), 4096);
    while !tree.refinable_leaves().is_empty() {
        for node in tree.refinable_leaves() {
            assert!(tree.refine(node));
        }
    }
    tree
}
#[test]
fn radial_lod_uses_resolution_fov_quality_and_retention() {
    let region = Region {
        origin: [0; 3],
        size: 8,
    };
    // Snapped camera [4,4,104] is exactly 100 units from this node's center.
    let camera = [3.9, 3.9, 103.9];
    let coarse = Lod::new(camera, 20, 90.0, 1.0);
    assert!(!coarse.needs_detail(region, 1.0)); // footprint is 10; size is 8.
    assert!(coarse.needs_detail(region, 1.5));
    assert!(Lod::new(camera, 40, 90.0, 1.0).needs_detail(region, 1.0));
    assert!(Lod::new(camera, 20, 60.0, 1.0).needs_detail(region, 1.0));
    assert!(Lod::new(camera, 20, 90.0, 2.0).needs_detail(region, 1.0));
    assert_eq!(
        coarse.needs_detail(region, 1.0),
        Lod::new([103.9, 3.9, 3.9], 20, 90.0, 1.0).needs_detail(region, 1.0)
    );
}
#[test]
fn rolling_sweep_bounds_reclamation_and_eventually_releases_far_detail() {
    let mut tree = loaded();
    let mut sweep = Residency::default();
    let near = Lod::new([4.; 3], 1080, 90., 1.);
    let full = tree.stats();
    for _ in 0..full.branch_capacity * 2 {
        assert_eq!(sweep.sweep(&mut tree, &near, 1, 1024).derefined, 0);
    }
    assert_eq!(full, tree.stats());
    let far = Lod::new([10000.; 3], 37, 90., 1.);
    assert_eq!(sweep.sweep(&mut tree, &far, 0, 1024).checked, 0);
    for _ in 0..full.branch_capacity * 4 {
        let before = tree.stats().data_nodes;
        let stats = sweep.sweep(&mut tree, &far, 1, 1024);
        assert_eq!(stats.checked, 1);
        assert!(stats.derefined <= 1);
        assert_eq!(before - tree.stats().data_nodes, stats.derefined * 8);
        tree.validate().unwrap();
    }
    assert_eq!(tree.stats().data_nodes, 1);
    assert_eq!(tree.stats().data_capacity, full.data_capacity);
    assert!(tree.refine(tree.root_node()));
    tree.validate().unwrap();
}
#[test]
fn automatic_unloading_skips_scenes_without_a_generation_source() {
    let mut tree = Tree::from_packed(&[9, 5, 3, 7, 3, 7, 3, 7, 3, 7], 3, 16).unwrap();
    let before = tree.stats();
    let result =
        Residency::default().sweep(&mut tree, &Lod::new([1e6; 3], 1, 90., 1.), 65536, 1024);
    assert_eq!(result.checked, 0);
    assert_eq!(tree.stats(), before);
}

#[test]
fn check_and_node_budgets_stop_independently_and_resume() {
    let far = Lod::new([10000.; 3], 37, 90., 1.);
    for budget in [0, 1, 7, 8, 9, 15, 16, 1024] {
        let mut tree = loaded();
        let mut sweep = Residency::default();
        let before = tree.stats().data_nodes;
        let stats = sweep.sweep(&mut tree, &far, 65536, budget);
        assert!(stats.unloaded <= budget);
        assert_eq!(before - tree.stats().data_nodes, stats.unloaded);
        assert_eq!(stats.unloaded, stats.derefined * 8);
        if budget < 8 {
            assert_eq!(stats.checked, 0);
        } else if budget < 24 {
            assert_eq!(stats.unloaded, budget / 8 * 8);
            // A fresh frame gets a fresh budget; the cursor continues through
            // the pool rather than repeatedly stopping on the same prefix.
            let next = sweep.sweep(&mut tree, &far, 65536, 8);
            assert_eq!(next.unloaded, 8);
        }
        tree.validate().unwrap();
    }
    let mut tree = loaded();
    let mut sweep = Residency::default();
    assert_eq!(sweep.sweep(&mut tree, &far, 0, 1024).checked, 0);
    let stats = sweep.sweep(&mut tree, &far, 2, 1024);
    assert_eq!(stats.checked, 2);
    assert!(stats.unloaded <= 16);
}
