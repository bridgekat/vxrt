// Stateless sample dimensions and solid-angle sampling for Lambertian transport.
const PI = 3.141592653589793;
const SUN_ANGLE = 0.1;

fn hash_u32(value: u32) -> u32 {
    var x = value;
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    return x ^ (x >> 16u);
}

fn sample_1d(pixel: u32, index: u32, dimension: u32, seed: u32) -> f32 {
    let key = hash_u32(pixel ^ hash_u32(seed + 0x9e3779b9u));
    let value = hash_u32(dimension ^ hash_u32(index ^ key));
    // Midpoints of 2^23 bins: strictly inside (0,1), including after f32 rounding.
    return (f32(value >> 9u) + 0.5) * (1.0 / 8388608.0);
}

fn path_sample_1d(pixel: u32, index: u32, dimension: u32, seed: u32, shared_samples: bool) -> f32 {
    // Shared bounce dimensions encourage spatially coherent rays. Camera jitter stays per-pixel.
    let key = select(pixel, 0u, shared_samples && dimension >= 2u);
    return sample_1d(key, index, dimension, seed);
}

fn local_to_world(axis: vec3<f32>, v: vec3<f32>) -> vec3<f32> {
    let helper = select(vec3<f32>(0, 0, 1), vec3<f32>(0, 1, 0), abs(axis.z) > 0.999);
    let tangent = normalize(cross(helper, axis));
    return normalize(tangent * v.x + cross(axis, tangent) * v.y + axis * v.z);
}

fn cosine_hemisphere(normal: vec3<f32>, u: vec2<f32>) -> vec3<f32> {
    let radius = sqrt(u.x);
    let phi = 2.0 * PI * u.y;
    return local_to_world(normal, vec3<f32>(radius * cos(phi), radius * sin(phi), sqrt(1.0 - u.x)));
}

fn cosine_pdf(normal: vec3<f32>, direction: vec3<f32>) -> f32 {
    return max(dot(normal, direction), 0.0) / PI;
}

fn sun_cone(axis: vec3<f32>, u: vec2<f32>) -> vec3<f32> {
    let z = 1.0 - u.x * (1.0 - cos(SUN_ANGLE * 0.5));
    let radius = sqrt(max(0.0, 1.0 - z * z));
    let phi = 2.0 * PI * u.y;
    return local_to_world(axis, vec3<f32>(radius * cos(phi), radius * sin(phi), z));
}

fn sun_pdf(axis: vec3<f32>, direction: vec3<f32>) -> f32 {
    if dot(axis, direction) < cos(SUN_ANGLE * 0.5) {
        return 0.0;
    }
    return 1.0 / (2.0 * PI * (1.0 - cos(SUN_ANGLE * 0.5)));
}

fn power_weight(pdf: f32, other_pdf: f32) -> f32 {
    let a = pdf * pdf;
    let b = other_pdf * other_pdf;
    if a + b == 0.0 {
        return 0.0;
    }
    return a / (a + b);
}
