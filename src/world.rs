//! Terrain height field and packed octree representation: internal/leaf tags in the low two bits.
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
        if p[1] >= max_height {
            nodes[index] = 3;
            return;
        }
        if size == 1 {
            nodes[index] = 3 | (u32::from((p[1] as i64) < heights[p[0] * world + p[2]]) << 2);
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
