use vxrt::core::{
    node::{self, NodeData, Region},
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
                            NodeData::EMPTY
                        } else if solid {
                            NodeData::SOLID
                        } else {
                            NodeData::MIXED
                        }
                    );
                    if size > 1 {
                        let children = node::split(
                            &map,
                            Region {
                                origin: [x, y, z],
                                size,
                            },
                        );
                        assert_eq!(node::merge_summarize(children), material);
                    }
                }
            }
        }
    }
}
#[test]
fn merges_only_compatible_exact_leaves() {
    assert_eq!(
        node::try_merge_exact([NodeData::EMPTY; 8]),
        Some(NodeData::EMPTY)
    );
    assert_eq!(
        node::try_merge_exact([NodeData::SOLID; 8]),
        Some(NodeData::SOLID)
    );
    assert_eq!(node::try_merge_exact([NodeData::MIXED; 8]), None);
    let mut mixed = [NodeData::SOLID; 8];
    mixed[3] = NodeData::EMPTY;
    assert_eq!(node::try_merge_exact(mixed), None);
}

#[test]
fn summaries_preserve_uncertainty_for_every_child_combination() {
    let states = [NodeData::EMPTY, NodeData::SOLID, NodeData::MIXED];
    for mut code in 0..3usize.pow(8) {
        let children = std::array::from_fn(|_| {
            let child = states[code % 3];
            code /= 3;
            child
        });
        let expected = if children.iter().all(|c| *c == NodeData::EMPTY) {
            NodeData::EMPTY
        } else if children.iter().all(|c| *c == NodeData::SOLID) {
            NodeData::SOLID
        } else {
            NodeData::MIXED
        };
        assert_eq!(node::merge_summarize(children), expected);
    }
}
