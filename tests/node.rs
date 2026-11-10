use vxrt::core::{
    node::{self, Leaf, Region},
    terrain::HeightMap,
};
#[test]
fn bounds_and_classification_match_every_voxel() {
    let heights: Vec<_> = (0..64).map(|i| ((i / 8) * 3 + i % 8) % 9).collect();
    let map = HeightMap::from_heights(8, &heights);
    for size in [1, 2, 4, 8] {
        for x in (0..8).step_by(size as usize) {
            for z in (0..8).step_by(size as usize) {
                let values: Vec<_> = (x..x + size)
                    .flat_map(|a| (z..z + size).map(move |b| (a * 8 + b) as usize))
                    .map(|i| heights[i])
                    .collect();
                assert_eq!(
                    map.bounds(x, z, size),
                    [*values.iter().min().unwrap(), *values.iter().max().unwrap()]
                );
                for y in (0..8).step_by(size as usize) {
                    let material = node::classify(
                        &map,
                        Region {
                            origin: [x, y, z],
                            size,
                        },
                    );
                    let empty = values.iter().all(|h| y >= *h);
                    let solid = values.iter().all(|h| y + size <= *h);
                    assert_eq!(
                        material,
                        if empty {
                            Leaf::EMPTY
                        } else if solid {
                            Leaf::SOLID
                        } else {
                            Leaf::DEFERRED
                        }
                    );
                }
            }
        }
    }
}
#[test]
fn merges_only_compatible_exact_leaves() {
    assert_eq!(node::merge([Leaf::EMPTY; 8]), Some(Leaf::EMPTY));
    assert_eq!(node::merge([Leaf::SOLID; 8]), Some(Leaf::SOLID));
    assert_eq!(node::merge([Leaf::DEFERRED; 8]), None);
    let mut mixed = [Leaf::SOLID; 8];
    mixed[3] = Leaf::EMPTY;
    assert_eq!(node::merge(mixed), None);
}
