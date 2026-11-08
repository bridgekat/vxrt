// Sparse-octree traversal, interactive shading, and Lambertian path transport.
struct Params {
    projection: mat4x4<f32>,
    model_view: mat4x4<f32>,
    camera: vec4<f32>,
    // xyz position, w field of view in radians
    frame: vec4<u32>,
    // width, height, path tracing, profiler
    beam: vec4<u32>,
    // previous index/size, current index/size
    scene: vec4<u32>,
    // world levels, march budget, reserved
    lighting: vec4<f32>,
    // LOD quality, sky intensity, sun intensity, reserved
    sampling: vec4<u32>,
    // sample index, seed, accumulated count, coherent bounce samples
}

struct Tree {
    count: u32,
    data: array<u32>
}

@group(0) @binding(0)
var<uniform> p: Params;

@group(0) @binding(1)
var<storage, read> tree: Tree;

@group(0) @binding(2)
var<storage, read_write> frame: array<vec4<f32>>;

@group(0) @binding(3)
var<storage, read_write> beams: array<f32>;

const ALBEDO = 0.8;

struct Intersection {
    pos: vec3<f32>,
    offset: vec3<f32>
}

var<private> root_size: f32;
var<private> lod_center: vec3<f32>;
var<private> lod_dir: vec3<f32>;
var<private> beam_mode: bool;
var<private> stack: array<vec4<f32>, 20>;
var<private> stp: u32;
// Per-invocation traversal state.
var<private> test_point: vec3<f32>;
var<private> last: Intersection;

fn sun_dir() -> vec3<f32> {
    return normalize(vec3<f32>(0.8, - 1.0, 0.3));
}

fn divide(v: vec4<f32>) -> vec3<f32> {
    return v.xyz / v.w;
}

fn inside(a: vec3<f32>, box: vec4<f32>) -> bool {
    return all(a >= box.xyz) && all(a < box.xyz + box.w);
}

fn sky(d: vec3<f32>) -> vec3<f32> {
    let dir = normalize(d);
    let sun = - sun_dir();
    let mu = clamp(dot(sun, dir), - 1.0, 1.0);
    if mu >= cos(SUN_ANGLE / 2.0) {
        return vec3<f32>(p.lighting.z);
    }
    // Sea-level RGB scattering coefficients (1/m), integrated over exponential
    // density profiles with Rayleigh/Mie scale heights of 8 km / 1.2 km.
    let rayleigh = vec3<f32>(5.8e-6, 13.5e-6, 33.1e-6) * 8000.0;
    let mie = vec3<f32>(21.0e-6) * 1200.0;
    let extinction = rayleigh + mie;
    // Approximate curved-atmosphere air mass, finite at the horizon. Extend
    // horizon density below it for environment rays escaping the voxel world.
    let elevation = max(dir.y, 0.0);
    let air_mass = 1.0 / sqrt(elevation * elevation + 0.0025);
    let sun_air_mass = 1.0 / sqrt(max(sun.y, 0.0) * max(sun.y, 0.0) + 0.0025);
    let transmittance = exp(- extinction * air_mass);
    // Evaluate solar attenuation at a representative midpoint of the column.
    let sunlight = exp(- extinction * (0.5 * sun_air_mass));
    let rayleigh_phase = 3.0 * (1.0 + mu * mu) / (16.0 * PI);
    // Henyey-Greenstein phase function models forward scattering by aerosols.
    let g = 0.76;
    let mie_phase = (1.0 - g * g) / (4.0 * PI * pow(1.0 + g * g - 2.0 * g * mu, 1.5));
    let scattering = (rayleigh * rayleigh_phase + mie * mie_phase) / extinction;
    // Art-directed solar exposure; keep radiance linear for path accumulation.
    return p.lighting.y * 12.0 * sunlight * scattering * (vec3<f32>(1.0) - transmittance);
}

var<private> pixel_index: u32;

fn random_dimension(dimension: u32) -> f32 {
    return path_sample_1d(pixel_index, p.sampling.x, dimension, p.sampling.y, p.sampling.w != 0u);
}

fn random_pair(dimension: u32) -> vec2<f32> {
    return vec2<f32>(random_dimension(dimension), random_dimension(dimension + 1u));
}

fn lod_check(level: u32, pos: vec3<u32>) -> bool {
    let rpos = (vec3<f32>(pos) + 0.5) * root_size / f32(1u << level) - lod_center;
    let size = root_size / f32(1u << level);
    let real = tan(p.camera.w / 2.0) * dot(rpos, lod_dir) * 2.0 / f32(p.frame.y);
    return size > real / p.lighting.x && (!beam_mode || size > real * f32(p.beam.w));
}

fn inner_intersect(org: vec3<f32>, dir: vec3<f32>, box: vec4<f32>) -> Intersection {
    let t = max((box.xyz - org) / dir, (box.xyz - org + box.w) / dir);
    let far = min(t.x, min(t.y, t.z));
    return Intersection(org + dir * far, select(vec3<f32>(0.0), sign(dir), t == vec3<f32>(far)));
}

fn outer_intersect(org: vec3<f32>, dir: vec3<f32>, box: vec4<f32>) -> Intersection {
    let a = min((box.xyz - org) / dir, (box.xyz - org + box.w) / dir);
    let b = max((box.xyz - org) / dir, (box.xyz - org + box.w) / dir);
    let near = max(a.x, max(a.y, a.z));
    let far = min(b.x, min(b.y, b.z));
    if far < near || near < 0.0 {
        return Intersection(org, vec3<f32>(0.0));
    }
    return Intersection(org + dir * near, select(vec3<f32>(0.0), sign(dir), a == vec3<f32>(near)));
}

fn cast_ray(reference: vec3<f32>, direction: vec3<f32>) -> f32 {
    let dir = normalize(direction);
    var box = vec4<f32>(0.0, 0.0, 0.0, root_size);
    if !inside(last.pos, box) {
        last = outer_intersect(last.pos, dir, box);
        if all(last.offset == vec3<f32>(0.0)) {
            return - 1.0;
        }
        test_point = clamp(last.pos, box.xyz + 0.5, box.xyz + box.w - 0.5);
    }
    var data: u32;
    if stp == 0u {
        data = tree.data[0u];
    }
    else {
        stp--;
        box = vec4<f32>(stack[stp].xyz, root_size / f32(1u << stp));
        data = bitcast<u32>(stack[stp].w);
    }
    for (var i = 0u; i < p.scene.y; i++) {
        while !inside(test_point, box) {
            if stp == 0u {
                return - 1.0;
            }
            stp--;
            box = vec4<f32>(stack[stp].xyz, root_size / f32(1u << stp));
            data = bitcast<u32>(stack[stp].w);
        }
        let pos = vec3<u32>(test_point);
        while (data & 3u) != 3u {
            if stp >= 20u {
                return 1.0;
            }
            stack[stp] = vec4<f32>(box.xyz, bitcast<f32>(data));
            stp++;
            var datap = data >> 2u;
            let mid = box.xyz + box.w / 2.0;
            if test_point.x >= mid.x {
                datap += 1u;
                box.x = mid.x;
            }
            if test_point.y >= mid.y {
                datap += 2u;
                box.y = mid.y;
            }
            if test_point.z >= mid.z {
                datap += 4u;
                box.z = mid.z;
            }
            box.w /= 2.0;
            data = tree.data[datap];
            if (data & 3u) != 3u && !lod_check(stp, pos >> vec3<u32>(p.scene.x - stp)) {
                // Treat an LOD stop as a solid leaf.
                data = 7u;
            }
        }
        if (data >> 2u) != 0u {
            return f32(i) / f32(p.scene.y);
        }
        let hit = inner_intersect(reference, dir, box);
        test_point = clamp(hit.pos, box.xyz + 0.5, box.xyz + box.w - 0.5) + hit.offset;
        last = hit;
    }
    return 1.0;
}

fn start_ray(org: vec3<f32>) {
    test_point = org;
    last = Intersection(org, vec3<f32>(0.0));
}

// Restart at the exact surface boundary, with a test point in the adjacent empty cell.
// Shadow and continuation rays have independent traversal stacks.
fn surface_ray(hit: Intersection, outside: vec3<f32>) {
    stp = 0u;
    test_point = outside;
    last = Intersection(hit.pos, - hit.offset);
}

fn trace_path(org: vec3<f32>, direction: vec3<f32>) -> vec3<f32> {
    start_ray(org);
    var dir = direction;
    var throughput = vec3<f32>(1.0);
    var radiance = vec3<f32>(0.0);
    var previous_pdf = 0.0;
    var bounce = 0u;
    loop {
        let distance = cast_ray(last.pos, dir);
        if distance < 0.0 {
            var weight = 1.0;
            if bounce > 0u {
                weight = power_weight(previous_pdf, sun_pdf(- sun_dir(), dir));
            }
            return radiance + throughput * sky(dir) * weight;
        }
        // A traversal-budget failure is not a surface hit or visible environment.
        if distance >= 1.0 {
            return radiance;
        }
        let hit = last;
        let outside = test_point - hit.offset;
        if dot(hit.offset, hit.offset) == 0.0 {
            return radiance;
        }
        // Neutral diffuse surfaces use geometric voxel-face normals.
        let n = normalize(- hit.offset);
        let dimension = 2u + bounce * 5u;
        // One light sample, uniform in solid angle over the solar disk.
        let light_dir = sun_cone(- sun_dir(), random_pair(dimension));
        let cosine = max(dot(n, light_dir), 0.0);
        let light_pdf = sun_pdf(- sun_dir(), light_dir);
        if cosine > 0.0 && light_pdf > 0.0 {
            surface_ray(hit, outside);
            if cast_ray(hit.pos, light_dir) < 0.0 {
                let bsdf_pdf = cosine / PI;
                radiance += throughput * ALBEDO * sky(light_dir) * (cosine / (PI * light_pdf)) * power_weight(light_pdf, bsdf_pdf);
            }
        }
        // Lambertian f*cos/pdf equals albedo for cosine-weighted sampling.
        throughput *= ALBEDO;
        dir = cosine_hemisphere(n, random_pair(dimension + 2u));
        previous_pdf = cosine_pdf(n, dir);
        bounce++;
        // Unbiased stochastic termination; no untested sky contribution or fixed bounce cutoff.
        if bounce >= 3u {
            let survive = clamp(max(throughput.x, max(throughput.y, throughput.z)), 0.05, 0.95);
            if random_dimension(dimension + 4u) >= survive {
                return radiance;
            }
            throughput /= survive;
        }
        surface_ray(hit, outside);
    }
    return radiance;
}

fn test_ray(reference: vec3<f32>, org: vec3<f32>, dir: vec3<f32>) -> vec3<f32> {
    start_ray(org);
    let distance = cast_ray(reference, dir);
    if p.frame.w != 0u {
        return vec3<f32>(max(distance, 0.0));
    }
    if distance < 0.0 {
        return sky(dir);
    }
    let n = - last.offset;
    var res = vec3<f32>(ALBEDO);
    res = mix(res * 0.2, res, clamp(dot(n, - sun_dir()), 0.0, 1.0));
    res = mix(sky(dir), res, clamp((1.0 - distance) * 2.0, 0.0, 1.0));
    return res;
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    root_size = f32(1u << p.scene.x);
    let pos = p.camera.xyz;
    let center_dir = normalize(divide(p.model_view * p.projection * vec4<f32>(0.0, 0.0, 1.0, 1.0)));
    lod_center = floor(pos) + 0.5;
    lod_dir = center_dir;
    beam_mode = p.beam.z < 1u;
    let pixel = id.xy;
    pixel_index = pixel.y * p.frame.x + pixel.x;
    let beam_width = (p.frame.x + 3u) / 4u + 1u;
    let extent = select(p.frame.xy, (p.frame.xy + vec2<u32>(3u)) / 4u + vec2<u32>(1u), beam_mode);
    if any(pixel >= extent) {
        return;
    }
    var beam_result = 0.0;
    if p.beam.x < 1u && p.frame.z == 0u {
        let base = pixel / (p.beam.y / p.beam.w);
        let index = base.y * beam_width + base.x;
        beam_result = min(min(beams[index], beams[index + 1u]), min(beams[index + beam_width], beams[index + beam_width + 1u])) - 1e-2;
    }
    let coords = vec2<f32>(pixel * p.beam.w) / vec2<f32>(p.frame.xy) * 2.0 - 1.0;
    var dithered = coords;
    if !beam_mode && p.frame.z != 0u {
        dithered += 2.0 * random_pair(0u) / vec2<f32>(p.frame.xy);
    }
    let dir = normalize(divide(p.model_view * p.projection * vec4<f32>(dithered, 1.0, 1.0)));
    if beam_mode {
        start_ray(pos + dir * beam_result);
        let distance = cast_ray(pos, dir);
        var value = 1e18;
        if distance >= 0.0 {
            let real = tan(p.camera.w / 2.0) * dot(last.pos - pos, lod_dir) * 2.0 / f32(p.frame.y);
            value = length(last.pos - pos) - real * f32(p.beam.w) / sqrt(2.0);
        }
        beams[pixel.y * beam_width + pixel.x] = value;
        return;
    }
    var color: vec3<f32>;
    if p.frame.z != 0u {
        color = trace_path(pos + dir * beam_result, dir);
    }
    else {
        color = test_ray(pos, pos + dir * beam_result, dir);
    }
    let index = pixel.y * p.frame.x + pixel.x;
    if p.frame.z != 0u && p.sampling.z > 0u {
        color = mix(frame[index].rgb, color, 1.0 / (f32(p.sampling.z) + 1.0));
    }
    frame[index] = vec4<f32>(color, 1.0);
}
