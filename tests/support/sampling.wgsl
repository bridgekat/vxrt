// Numerical diagnostics use exactly the helpers compiled into the renderer.
struct Result  {
    random: vec4<f32>,
    geometry: vec4<f32>,
    estimates: vec4<f32>,
    independent: vec4<f32>,
    coherence: vec4<f32>,
}
@group(0) @binding(0) var<storage, read_write> results: array<Result>;
fn direct(normal: vec3<f32>, axis: vec3<f32>, direction: vec3<f32>) -> f32  {
    let light_pdf = sun_pdf(axis, direction);
    if light_pdf == 0.0  {
        return 0.0;
    }
    let bsdf_pdf = cosine_pdf(normal, direction);
    return bsdf_pdf / light_pdf * power_weight(light_pdf, bsdf_pdf);
}
fn escaped_sun(normal: vec3<f32>, axis: vec3<f32>, direction: vec3<f32>) -> f32  {
    let light_pdf = sun_pdf(axis, direction);
    if light_pdf == 0.0  {
        return 0.0;
    }
    return power_weight(cosine_pdf(normal, direction), light_pdf);
}
@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) id: vec3<u32>)  {
    let i = id.x;
    let pixel = i / 256u;
    let index = i % 256u;
    let a = sample_1d(pixel, index, 0u, 1u);
    let b = sample_1d(pixel, index, 1u, 1u);
    let u = vec2<f32>(a,b);
    let v = vec2<f32>(sample_1d(pixel,index,2u,1u),sample_1d(pixel,index,3u,1u));
    let n = vec3<f32>(0,1,0);
    let bsdf = cosine_hemisphere(n, u);
    let light = sun_cone(n, v);
    let tilted = normalize(vec3<f32>(sqrt(0.75),0.5,0.0));
    let horizon = vec3<f32>(1,0,0);
    results[i].random = vec4<f32>(a,b,dot(n,bsdf),dot(n,light));
    results[i].geometry = vec4<f32>(length(bsdf),length(light),dot(n,cosine_hemisphere(-n,u)),direct(n,-n,sun_cone(-n,v)));
    let light_estimate = direct(n,n,light);
    results[i].estimates = vec4<f32>(
    light_estimate + escaped_sun(n,n,bsdf),
    light_estimate + power_weight(cosine_pdf(n,bsdf),sun_pdf(n,bsdf)),
    direct(n,tilted,sun_cone(tilted,v)) + escaped_sun(n,tilted,bsdf),
    direct(n,horizon,sun_cone(horizon,v)) + escaped_sun(n,horizon,bsdf));
    results[i].coherence = vec4<f32>(path_sample_1d(0u,i,2u,1u,true)-path_sample_1d(1u,i,2u,1u,true), path_sample_1d(0u,i,0u,1u,true)-path_sample_1d(1u,i,0u,1u,true), path_sample_1d(0u,i,2u,1u,false)-path_sample_1d(1u,i,2u,1u,false), path_sample_1d(0u,i,2u,1u,true)-path_sample_1d(0u,i+1u,2u,1u,true));
    results[i].independent = vec4<f32>(sample_1d(0u,i,0u,1u),sample_1d(1u,i,0u,1u),sample_1d(0u,i,0u,2u),sample_1d(0u,i+16777216u,0u,1u));
}
