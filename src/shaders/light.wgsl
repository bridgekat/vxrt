struct Camera {
  view_pos: vec4<f32>,
  view_proj: mat4x4<f32>,
}

struct Light {
  position: vec3<f32>,
  color: vec3<f32>,
}

struct VertexInput {
  @location(0) position: vec3<f32>,
};

struct VertexOutput {
  @builtin(position) clip_position: vec4<f32>,
  @location(0) color: vec3<f32>,
};

@group(0) @binding(0) var<uniform> camera: Camera;
@group(1) @binding(0) var<uniform> light: Light;

@vertex
fn vs_main(model: VertexInput) -> VertexOutput {
  let scale = 0.25;
  var out: VertexOutput;
  out.clip_position = camera.view_proj * vec4<f32>(model.position * scale + light.position, 1.0);
  out.color = light.color;
  return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
  return vec4<f32>(in.color, 1.0);
}
