struct Params  {
    projection: mat4x4<f32>, model_view: mat4x4<f32>, camera:vec4<f32>,
    frame:vec4<u32>, beam:vec4<u32>, world:vec4<u32>, misc:vec4<f32>, sampling:vec4<u32>,
}
@group(0) @binding(0) var<uniform> p:Params;
@group(0) @binding(1) var<storage,read> pixels:array<vec4<f32>>;
struct Vertex  {
    @builtin(position) position:vec4<f32>, @location(0) uv:vec2<f32>
}
@vertex fn vs_main(@builtin(vertex_index) index:u32)->Vertex  {
    let positions=array<vec2<f32>,3>(vec2<f32>(-1,-1),vec2<f32>(3,-1),vec2<f32>(-1,3));
    let pos=positions[index];
    return Vertex(vec4<f32>(pos,0,1),pos*0.5+0.5);
}
@fragment fn fs_main(v:Vertex)->@location(0) vec4<f32>  {
    // Bilinearly sample the linear frame before gamma encoding.
    let xy=v.uv*vec2<f32>(p.frame.xy)-0.5;
    let base=vec2<i32>(floor(xy));
    let f=fract(xy);
    let limit=vec2<i32>(p.frame.xy)-1;
    let a=clamp(base,vec2<i32>(0),limit);
    let b=clamp(base+vec2<i32>(1),vec2<i32>(0),limit);
    let color=mix(mix(pixels[u32(a.y)*p.frame.x+u32(a.x)].rgb,pixels[u32(a.y)*p.frame.x+u32(b.x)].rgb,f.x),mix(pixels[u32(b.y)*p.frame.x+u32(a.x)].rgb,pixels[u32(b.y)*p.frame.x+u32(b.x)].rgb,f.x),f.y);
    return vec4<f32>(pow(max(color,vec3<f32>(0)),vec3<f32>(1.0/2.2)),1);
}
