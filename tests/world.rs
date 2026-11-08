use vxrt::world::*;
#[test]
fn tree_matches_height_field() {
    let tree = generate(8, 256);
    assert_eq!(tree[0] as usize, tree.len() - 1);
    assert!(tree.len() > 10, "exercise branches, not just a solid root");
    for x in (0..256).step_by(17) {
        for y in (0..256).step_by(13) {
            for z in (0..256).step_by(19) {
                let mut ptr = 0;
                for bit in (0..=8).rev() {
                    let n = tree[ptr + 1];
                    if n & 3 == 3 {
                        assert_eq!(n >> 2, u32::from((y as i64) < height(x as f64, z as f64)));
                        break;
                    }
                    assert!(bit > 0);
                    ptr = (n >> 2) as usize
                        + ((x >> (bit - 1)) & 1)
                        + (((y >> (bit - 1)) & 1) << 1)
                        + (((z >> (bit - 1)) & 1) << 2);
                }
            }
        }
    }
}

#[test]
fn height_cutoff_applies_at_voxel_resolution() {
    for max_height in [0, 1, 3, 7, 8] {
        let tree = generate(3, max_height);
        vxrt::octree::validate(&tree, 3).unwrap();
        for x in 0..8 {
            for y in 0..8 {
                for z in 0..8 {
                    let mut ptr = 0;
                    for bit in (0..=3).rev() {
                        let node = tree[ptr + 1];
                        if node & 3 == 3 {
                            assert_eq!(
                                node >> 2,
                                u32::from(
                                    y < max_height && (y as i64) < height(x as f64, z as f64)
                                ),
                                "cutoff={max_height}, voxel=({x},{y},{z})"
                            );
                            break;
                        }
                        assert!(bit > 0);
                        ptr = ((node >> 2)
                            + ((x >> (bit - 1)) & 1)
                            + (((y >> (bit - 1)) & 1) << 1)
                            + (((z >> (bit - 1)) & 1) << 2)) as usize;
                    }
                }
            }
        }
    }
}
