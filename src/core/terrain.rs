//! Procedural terrain height function and immutable min/max height pyramid.
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
/// Baked, clipped column heights and aligned XZ min/max reductions.
pub struct HeightMap {
    levels: Vec<Vec<[u32; 2]>>,
    side: u32,
}
impl HeightMap {
    pub fn new(levels: u32, max_height: u32) -> Self {
        assert!((1..=10).contains(&levels));
        let side = 1u32 << levels;
        let base = (0..side * side)
            .map(|i| {
                let h = height((i / side) as f64, (i % side) as f64)
                    .clamp(0, max_height.min(side) as i64) as u32;
                [h, h]
            })
            .collect();
        Self::reduce(side, base)
    }
    pub fn from_heights(side: u32, heights: &[u32]) -> Self {
        assert!(side.is_power_of_two() && heights.len() == side as usize * side as usize);
        Self::reduce(side, heights.iter().map(|h| [(*h).min(side); 2]).collect())
    }
    fn reduce(side: u32, base: Vec<[u32; 2]>) -> Self {
        let mut levels = vec![base];
        let mut width = side as usize;
        while width > 1 {
            let prev = levels.last().unwrap();
            let next = width / 2;
            let mut data = Vec::with_capacity(next * next);
            for x in 0..next {
                for z in 0..next {
                    let children = [
                        prev[(x * 2) * width + z * 2],
                        prev[(x * 2 + 1) * width + z * 2],
                        prev[(x * 2) * width + z * 2 + 1],
                        prev[(x * 2 + 1) * width + z * 2 + 1],
                    ];
                    data.push([
                        children.iter().map(|b| b[0]).min().unwrap(),
                        children.iter().map(|b| b[1]).max().unwrap(),
                    ]);
                }
            }
            levels.push(data);
            width = next;
        }
        Self { levels, side }
    }
    pub fn bytes(&self) -> usize {
        self.levels
            .iter()
            .map(|level| level.len() * std::mem::size_of::<[u32; 2]>())
            .sum()
    }
    pub fn side(&self) -> u32 {
        self.side
    }
    pub fn bounds(&self, x: u32, z: u32, size: u32) -> [u32; 2] {
        assert!(
            size.is_power_of_two()
                && size <= self.side
                && x.is_multiple_of(size)
                && z.is_multiple_of(size)
        );
        self.levels[size.trailing_zeros() as usize]
            [((x / size) * (self.side / size) + z / size) as usize]
    }
}
