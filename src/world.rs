//! Terrain height field and packed octree representation: generated/leaf in low two bits.
fn noise(x: i64, y: i64) -> f64 {
    let n = x
        .wrapping_mul(107)
        .wrapping_add(y.wrapping_mul(13258953287));
    let n = (n >> 13) ^ n;
    let hash = n
        .wrapping_mul(n.wrapping_mul(n).wrapping_mul(15731).wrapping_add(789221))
        .wrapping_add(1376312589);
    (hash & 0x7fffffff) as f64 / 16777216.0
}
fn fractal(x: f64, y: f64) -> f64 {
    let (mut total, mut frequency, mut amplitude) = (0.0, 1.0, 1.0);
    for _ in 0..5 {
        let (x, y) = (x * frequency, y * frequency);
        let (ix, iy) = (x.floor() as i64, y.floor() as i64);
        let (fx, fy) = (x - x.floor(), y - y.floor());
        let lerp = |a: f64, b: f64, t: f64| a * (1.0 - t) + b * t;
        total += lerp(
            lerp(noise(ix, iy), noise(ix + 1, iy), fx),
            lerp(noise(ix, iy + 1), noise(ix + 1, iy + 1), fx),
            fy,
        ) * amplitude;
        frequency *= 2.0;
        amplitude *= 0.5;
    }
    total
}
pub fn height(x: f64, z: f64) -> i64 {
    let mountain = fractal(x / 128.0 + 113.0, z / 128.0 + 1301.0) as i64;
    let lower = (fractal(x / 64.0 + 0.125, z / 64.0 + 0.125) as i64) >> 3;
    let upper = lower + 96;
    let transition = fractal(x / 64.0 + 113.0, z / 64.0 + 1301.0) as i64;
    let base = (fractal(x / 1024.0, z / 1024.0) as i64) * 2 - 320;
    (if transition > upper {
        mountain.max(upper)
    } else {
        transition.max(lower)
    }) + base
        + 64
}
pub fn generate(levels: u32, max_height: u32) -> Vec<u32> {
    let size = 1usize << levels;
    let heights: Vec<i64> = (0..size * size)
        .map(|i| height((i / size) as f64, (i % size) as f64))
        .collect();
    fn node(
        nodes: &mut Vec<u32>,
        heights: &[i64],
        world: usize,
        max_height: usize,
        index: usize,
        p: [usize; 3],
        size: usize,
    ) {
        if size == 1 {
            nodes[index] = 3 | (u32::from((p[1] as i64) < heights[p[0] * world + p[2]]) << 2);
            return;
        }
        if p[1] >= max_height {
            nodes[index] = 3;
            return;
        }
        let ptr = nodes.len();
        nodes[index] = (ptr as u32) << 2 | 1;
        nodes.resize(ptr + 8, 0);
        let half = size / 2;
        for i in 0..8 {
            node(
                nodes,
                heights,
                world,
                max_height,
                ptr + i,
                [
                    p[0] + (i & 1) * half,
                    p[1] + ((i >> 1) & 1) * half,
                    p[2] + ((i >> 2) & 1) * half,
                ],
                half,
            );
        }
        if nodes.len() == ptr + 8 && nodes[ptr..].iter().all(|&n| n == nodes[ptr]) {
            nodes[index] = nodes[ptr];
            nodes.truncate(ptr);
        }
    }
    let mut nodes = vec![0];
    node(
        &mut nodes,
        &heights,
        size,
        max_height as usize,
        0,
        [0; 3],
        size,
    );
    nodes.insert(0, nodes.len() as u32);
    nodes
}
pub fn noise_maps(levels: u32, seed: u32) -> [Vec<Vec<u8>>; 3] {
    let size = 1usize << levels;
    let mut state = seed;
    let mut next = || {
        state = state.wrapping_mul(1664525).wrapping_add(1013904223);
        (state >> 24) as u8
    };
    let mut base = vec![0; size * size * 4];
    for x in 0..size {
        for y in 0..size {
            let a = next();
            let b = next();
            base[(y * size + x) * 4..(y * size + x) * 4 + 4].copy_from_slice(&[a, b, a, b]);
        }
    }
    let bounds = |maximum: bool| {
        let mut first = vec![0; size * size * 4];
        for y in 0..size {
            for x in 0..size {
                let v = [
                    base[(y * size + x) * 4],
                    base[(y * size + (x + 1) % size) * 4],
                    base[(((y + 1) % size) * size + x) * 4],
                    base[(((y + 1) % size) * size + (x + 1) % size) * 4],
                ];
                first[(y * size + x) * 4] = if maximum {
                    *v.iter().max().unwrap()
                } else {
                    *v.iter().min().unwrap()
                };
            }
        }
        let mut result = vec![first];
        let mut width = size;
        while width > 1 {
            let prev = result.last().unwrap();
            let w = width / 2;
            let mut curr = vec![0; w * w * 4];
            for y in 0..w {
                for x in 0..w {
                    for c in 0..4 {
                        let v = [
                            prev[((y * 2) * width + x * 2) * 4 + c],
                            prev[((y * 2) * width + x * 2 + 1) * 4 + c],
                            prev[((y * 2 + 1) * width + x * 2) * 4 + c],
                            prev[((y * 2 + 1) * width + x * 2 + 1) * 4 + c],
                        ];
                        curr[(y * w + x) * 4 + c] = if maximum {
                            *v.iter().max().unwrap()
                        } else {
                            *v.iter().min().unwrap()
                        };
                    }
                }
            }
            result.push(curr);
            width = w;
        }
        result
    };
    let max = bounds(true);
    let min = bounds(false);
    [vec![base], max, min]
}
#[cfg(test)]
mod tests {
    use super::*;
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
    fn bounds_cover_noise() {
        let maps = noise_maps(3, 1);
        let reds: Vec<_> = maps[0][0].iter().step_by(4).copied().collect();
        assert_eq!(maps[1][3][0], *reds.iter().max().unwrap());
        assert_eq!(maps[2][3][0], *reds.iter().min().unwrap());
    }
}
