use std::iter;

use cgmath::prelude::*;
use wgpu::util::DeviceExt;
use winit::{
  event::*,
  event_loop::{ControlFlow, EventLoop},
  window::Window,
};

mod camera;
mod model;
mod resources;
mod texture;

use model::{DrawLight, DrawModel, Vertex};

const MULTISAMPLE_SAMPLES: u32 = 4;
const NUM_INSTANCES_PER_ROW: u32 = 10;

// Note: uniforms require 16 byte (4 float) alignment.
#[repr(C)]
#[derive(Debug, Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
struct CameraUniform {
  view_position: [f32; 4],
  view_proj: [[f32; 4]; 4],
}

impl CameraUniform {
  fn new() -> Self {
    Self { view_position: [0.0; 4], view_proj: cgmath::Matrix4::identity().into() }
  }

  fn update_view_proj(&mut self, camera: &camera::Camera, projection: &camera::Projection) {
    self.view_position = camera.position.to_homogeneous().into();
    self.view_proj = (projection.calc_matrix() * camera.calc_matrix()).into()
  }
}

// Note: uniforms require 16 byte (4 float) alignment.
#[repr(C)]
#[derive(Debug, Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
struct LightUniform {
  position: [f32; 3],
  _padding: u32,
  color: [f32; 3],
  _padding2: u32,
}

impl LightUniform {
  fn new() -> Self {
    Self { position: [0.0, 0.0, 0.0], _padding: 0, color: [1.0, 1.0, 1.0], _padding2: 0 }
  }

  fn update_position(&mut self, position: [f32; 3]) {
    self.position = position;
  }
}

struct Instance {
  position: cgmath::Vector3<f32>,
  rotation: cgmath::Quaternion<f32>,
}

impl Instance {
  fn to_raw(&self) -> InstanceRaw {
    InstanceRaw {
      model: (cgmath::Matrix4::from_translation(self.position) * cgmath::Matrix4::from(self.rotation)).into(),
      normal: cgmath::Matrix3::from(self.rotation).into(),
    }
  }
}

#[repr(C)]
#[derive(Debug, Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
struct InstanceRaw {
  model: [[f32; 4]; 4],
  normal: [[f32; 3]; 3],
}

impl model::Vertex for InstanceRaw {
  fn desc() -> wgpu::VertexBufferLayout<'static> {
    use std::mem;
    wgpu::VertexBufferLayout {
      array_stride: mem::size_of::<InstanceRaw>() as wgpu::BufferAddress,
      // We need to switch from using a step mode of Vertex to Instance
      // This means that our shaders will only change to use the next
      // instance when the shader starts processing a new instance
      step_mode: wgpu::VertexStepMode::Instance,
      attributes: &[
        wgpu::VertexAttribute {
          offset: 0,
          // While our vertex shader only uses locations 0, and 1 now, in later tutorials we'll
          // be using 2, 3, and 4, for Vertex. We'll start at slot 5 not conflict with them later
          shader_location: 5,
          format: wgpu::VertexFormat::Float32x4,
        },
        // A mat4 takes up 4 vertex slots as it is technically 4 vec4s. We need to define a slot
        // for each vec4. We don't have to do this in code though.
        wgpu::VertexAttribute {
          offset: mem::size_of::<[f32; 4]>() as wgpu::BufferAddress,
          shader_location: 6,
          format: wgpu::VertexFormat::Float32x4,
        },
        wgpu::VertexAttribute {
          offset: mem::size_of::<[f32; 8]>() as wgpu::BufferAddress,
          shader_location: 7,
          format: wgpu::VertexFormat::Float32x4,
        },
        wgpu::VertexAttribute {
          offset: mem::size_of::<[f32; 12]>() as wgpu::BufferAddress,
          shader_location: 8,
          format: wgpu::VertexFormat::Float32x4,
        },
        wgpu::VertexAttribute {
          offset: mem::size_of::<[f32; 16]>() as wgpu::BufferAddress,
          shader_location: 9,
          format: wgpu::VertexFormat::Float32x3,
        },
        wgpu::VertexAttribute {
          offset: mem::size_of::<[f32; 19]>() as wgpu::BufferAddress,
          shader_location: 10,
          format: wgpu::VertexFormat::Float32x3,
        },
        wgpu::VertexAttribute {
          offset: mem::size_of::<[f32; 22]>() as wgpu::BufferAddress,
          shader_location: 11,
          format: wgpu::VertexFormat::Float32x3,
        },
      ],
    }
  }
}

struct State {
  surface: wgpu::Surface,
  device: wgpu::Device,
  queue: wgpu::Queue,
  surface_size: winit::dpi::PhysicalSize<u32>,
  surface_format: wgpu::TextureFormat,
  surface_present_mode: wgpu::PresentMode,
  camera: camera::Camera,
  projection: camera::Projection,
  frame_color_texture: texture::Texture,
  frame_depth_texture: texture::Texture,
  camera_controller: camera::CameraController,
  camera_uniform: CameraUniform,
  light_uniform: LightUniform,
  camera_buffer: wgpu::Buffer,
  light_buffer: wgpu::Buffer,
  camera_bind_group: wgpu::BindGroup,
  light_bind_group: wgpu::BindGroup,
  instances: Vec<Instance>,
  instance_buffer: wgpu::Buffer,
  render_pipeline: wgpu::RenderPipeline,
  light_render_pipeline: wgpu::RenderPipeline,
  obj_model: model::Model,
  #[allow(dead_code)]
  debug_material: model::Material,
  mouse_pressed: bool,
  window: Window,
}

impl State {
  fn create_render_pipeline(
    device: &wgpu::Device,
    layout: &wgpu::PipelineLayout,
    color_format: wgpu::TextureFormat,
    depth_format: wgpu::TextureFormat,
    vertex_layouts: &[wgpu::VertexBufferLayout],
    shader: wgpu::ShaderModuleDescriptor,
  ) -> wgpu::RenderPipeline {
    let shader = device.create_shader_module(shader);

    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
      label: Some(&format!("{:?}", shader)),
      layout: Some(layout),
      vertex: wgpu::VertexState { module: &shader, entry_point: "vs_main", buffers: vertex_layouts },
      primitive: wgpu::PrimitiveState {
        topology: wgpu::PrimitiveTopology::TriangleList,
        strip_index_format: None,
        front_face: wgpu::FrontFace::Ccw,
        cull_mode: Some(wgpu::Face::Back),
        polygon_mode: wgpu::PolygonMode::Fill, // Anything other than Fill requires Features::NON_FILL_POLYGON_MODE
        unclipped_depth: false,                // Requires Features::DEPTH_CLIP_CONTROL
        conservative: false,                   // Requires Features::CONSERVATIVE_RASTERIZATION
      },
      depth_stencil: Some(wgpu::DepthStencilState {
        format: depth_format,
        depth_write_enabled: true,
        depth_compare: wgpu::CompareFunction::Less,
        stencil: wgpu::StencilState::default(),
        bias: wgpu::DepthBiasState::default(),
      }),
      multisample: wgpu::MultisampleState { count: MULTISAMPLE_SAMPLES, mask: !0, alpha_to_coverage_enabled: false },
      fragment: Some(wgpu::FragmentState {
        module: &shader,
        entry_point: "fs_main",
        targets: &[Some(wgpu::ColorTargetState {
          format: color_format,
          blend: Some(wgpu::BlendState { alpha: wgpu::BlendComponent::REPLACE, color: wgpu::BlendComponent::REPLACE }),
          write_mask: wgpu::ColorWrites::ALL,
        })],
      }),
      // If the pipeline will be used with a multiview render pass, this
      // indicates how many array layers the attachments will have.
      multiview: None,
    })
  }

  async fn new<T: 'static>(event_loop: &EventLoop<T>) -> Self {
    // Create window.
    let window = winit::window::WindowBuilder::new().with_title("vxrt").build(event_loop).unwrap();

    // Initialise wgpu.
    let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
      backends: wgpu::Backends::PRIMARY,
      flags: wgpu::InstanceFlags::from_build_config(),
      dx12_shader_compiler: wgpu::Dx12Compiler::default(),
      gles_minor_version: wgpu::Gles3MinorVersion::default(),
    });

    // Create surface. Since `window` is dropped after `surface`, this is safe.
    let surface = unsafe { instance.create_surface(&window) }.unwrap();

    // Select device.
    let adapter = instance
      .request_adapter(&wgpu::RequestAdapterOptions {
        compatible_surface: Some(&surface),
        power_preference: wgpu::PowerPreference::HighPerformance,
        force_fallback_adapter: false,
      })
      .await
      .unwrap();

    // Initialise device context.
    let (device, queue) = adapter
      .request_device(
        &wgpu::DeviceDescriptor {
          label: Some("device"),
          features: wgpu::Features::empty(),
          limits: wgpu::Limits::default(),
        },
        None,
      )
      .await
      .unwrap();

    // Initialise surface.
    let surface_size = window.inner_size();
    let surface_format = wgpu::TextureFormat::Bgra8Unorm;
    let surface_present_mode = wgpu::PresentMode::AutoVsync;
    // let surface_caps = surface.get_capabilities(&adapter);
    surface.configure(
      &device,
      &wgpu::SurfaceConfiguration {
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
        format: surface_format,
        width: surface_size.width,
        height: surface_size.height,
        present_mode: surface_present_mode,
        alpha_mode: wgpu::CompositeAlphaMode::Opaque,
        view_formats: vec![],
      },
    );

    // Create framebuffers.
    let frame_color_texture = texture::Texture::create_frame_color_texture(
      &device,
      "frame_color_texture",
      surface_size.width,
      surface_size.height,
      MULTISAMPLE_SAMPLES,
    );

    let frame_depth_texture = texture::Texture::create_frame_depth_texture(
      &device,
      "frame_depth_texture",
      surface_size.width,
      surface_size.height,
      MULTISAMPLE_SAMPLES,
    );

    // Create uniform buffers.
    let camera_buffer = device.create_buffer(&wgpu::BufferDescriptor {
      label: Some("camera_buffer"),
      size: std::mem::size_of::<CameraUniform>() as u64,
      usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
      mapped_at_creation: false,
    });

    let light_buffer = device.create_buffer(&wgpu::BufferDescriptor {
      label: Some("light_buffer"),
      size: std::mem::size_of::<LightUniform>() as u64,
      usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
      mapped_at_creation: false,
    });

    // Specify the types of textures and samplers to be bound.
    let texture_bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
      label: Some("texture_bind_group_layout"),
      entries: &[
        wgpu::BindGroupLayoutEntry {
          binding: 0,
          visibility: wgpu::ShaderStages::FRAGMENT,
          ty: wgpu::BindingType::Texture {
            sample_type: wgpu::TextureSampleType::Float { filterable: true },
            view_dimension: wgpu::TextureViewDimension::D2,
            multisampled: false,
          },
          count: None,
        },
        wgpu::BindGroupLayoutEntry {
          binding: 1,
          visibility: wgpu::ShaderStages::FRAGMENT,
          ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
          count: None,
        },
        wgpu::BindGroupLayoutEntry {
          binding: 2,
          visibility: wgpu::ShaderStages::FRAGMENT,
          ty: wgpu::BindingType::Texture {
            sample_type: wgpu::TextureSampleType::Float { filterable: true },
            view_dimension: wgpu::TextureViewDimension::D2,
            multisampled: false,
          },
          count: None,
        },
        wgpu::BindGroupLayoutEntry {
          binding: 3,
          visibility: wgpu::ShaderStages::FRAGMENT,
          ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
          count: None,
        },
      ],
    });

    // Specify the types of uniforms to be bound.
    let camera_bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
      label: Some("camera_bind_group_layout"),
      entries: &[wgpu::BindGroupLayoutEntry {
        binding: 0,
        visibility: wgpu::ShaderStages::VERTEX | wgpu::ShaderStages::FRAGMENT,
        ty: wgpu::BindingType::Buffer {
          ty: wgpu::BufferBindingType::Uniform,
          has_dynamic_offset: false,
          min_binding_size: None,
        },
        count: None,
      }],
    });

    let light_bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
      label: Some("light_bind_group_layout"),
      entries: &[wgpu::BindGroupLayoutEntry {
        binding: 0,
        visibility: wgpu::ShaderStages::VERTEX | wgpu::ShaderStages::FRAGMENT,
        ty: wgpu::BindingType::Buffer {
          ty: wgpu::BufferBindingType::Uniform,
          has_dynamic_offset: false,
          min_binding_size: None,
        },
        count: None,
      }],
    });

    let camera_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
      label: Some("camera_bind_group"),
      layout: &camera_bind_group_layout,
      entries: &[wgpu::BindGroupEntry { binding: 0, resource: camera_buffer.as_entire_binding() }],
    });

    let light_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
      label: Some("light_bind_group"),
      layout: &light_bind_group_layout,
      entries: &[wgpu::BindGroupEntry { binding: 0, resource: light_buffer.as_entire_binding() }],
    });

    // Upload initial data to uniform buffers.
    let camera = camera::Camera::new((0.0, 5.0, 10.0), cgmath::Deg(-90.0), cgmath::Deg(-20.0));
    let projection = camera::Projection::new(surface_size.width, surface_size.height, cgmath::Deg(45.0), 0.1, 100.0);
    let camera_controller = camera::CameraController::new(4.0, 0.4);

    let mut camera_uniform = CameraUniform::new();
    camera_uniform.update_view_proj(&camera, &projection);

    let mut light_uniform = LightUniform::new();
    light_uniform.update_position([2.0, 2.0, 2.0]);

    queue.write_buffer(&camera_buffer, 0, bytemuck::cast_slice(&[camera_uniform]));
    queue.write_buffer(&light_buffer, 0, bytemuck::cast_slice(&[light_uniform]));

    // ???
    const SPACE_BETWEEN: f32 = 3.0;
    let instances = (0..NUM_INSTANCES_PER_ROW)
      .flat_map(|z| {
        (0..NUM_INSTANCES_PER_ROW).map(move |x| {
          let x = SPACE_BETWEEN * (x as f32 - NUM_INSTANCES_PER_ROW as f32 / 2.0);
          let z = SPACE_BETWEEN * (z as f32 - NUM_INSTANCES_PER_ROW as f32 / 2.0);
          let position = cgmath::Vector3 { x, y: 0.0, z };
          let rotation = if position.is_zero() {
            cgmath::Quaternion::from_axis_angle(cgmath::Vector3::unit_z(), cgmath::Deg(0.0))
          } else {
            cgmath::Quaternion::from_axis_angle(position.normalize(), cgmath::Deg(45.0))
          };
          Instance { position, rotation }
        })
      })
      .collect::<Vec<_>>();

    let instance_data = instances.iter().map(Instance::to_raw).collect::<Vec<_>>();
    let instance_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
      label: Some("instance_buffer"),
      contents: bytemuck::cast_slice(&instance_data),
      usage: wgpu::BufferUsages::VERTEX,
    });

    let obj_model = resources::load_model("cube.obj", &device, &queue, &texture_bind_group_layout).await.unwrap();

    let render_pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
      label: Some("Render Pipeline Layout"),
      bind_group_layouts: &[&texture_bind_group_layout, &camera_bind_group_layout, &light_bind_group_layout],
      push_constant_ranges: &[],
    });

    let render_pipeline = {
      let shader = wgpu::ShaderModuleDescriptor {
        label: Some("Normal Shader"),
        source: wgpu::ShaderSource::Wgsl(include_str!("shaders/shader.wgsl").into()),
      };
      Self::create_render_pipeline(
        &device,
        &render_pipeline_layout,
        surface_format,
        texture::Texture::FRAME_DEPTH_FORMAT,
        &[model::ModelVertex::desc(), InstanceRaw::desc()],
        shader,
      )
    };

    let light_render_pipeline = {
      let layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("Light Pipeline Layout"),
        bind_group_layouts: &[&camera_bind_group_layout, &light_bind_group_layout],
        push_constant_ranges: &[],
      });
      let shader = wgpu::ShaderModuleDescriptor {
        label: Some("Light Shader"),
        source: wgpu::ShaderSource::Wgsl(include_str!("shaders/light.wgsl").into()),
      };
      Self::create_render_pipeline(
        &device,
        &layout,
        surface_format,
        texture::Texture::FRAME_DEPTH_FORMAT,
        &[model::ModelVertex::desc()],
        shader,
      )
    };

    let debug_material = {
      let diffuse_bytes = include_bytes!("../assets/cobble-diffuse.png");
      let normal_bytes = include_bytes!("../assets/cobble-normal.png");

      let diffuse_texture =
        texture::Texture::from_bytes(&device, &queue, diffuse_bytes, "res/alt-diffuse.png", false).unwrap();
      let normal_texture =
        texture::Texture::from_bytes(&device, &queue, normal_bytes, "res/alt-normal.png", true).unwrap();

      model::Material::new(&device, "alt-material", diffuse_texture, normal_texture, &texture_bind_group_layout)
    };

    Self {
      surface,
      device,
      queue,
      surface_size,
      surface_format,
      surface_present_mode,
      camera,
      projection,
      frame_color_texture,
      frame_depth_texture,
      camera_controller,
      camera_uniform,
      light_uniform,
      camera_buffer,
      light_buffer,
      camera_bind_group,
      light_bind_group,
      instances,
      instance_buffer,
      render_pipeline,
      light_render_pipeline,
      obj_model,
      debug_material,
      mouse_pressed: false,
      window,
    }
  }

  pub fn window(&self) -> &Window {
    &self.window
  }

  fn resize(&mut self, new_size: winit::dpi::PhysicalSize<u32>) {
    if new_size.width > 0 && new_size.height > 0 {
      self.surface_size = new_size;
      self.projection.resize(self.surface_size.width, self.surface_size.height);
      self.surface.configure(
        &self.device,
        &wgpu::SurfaceConfiguration {
          usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
          format: self.surface_format,
          width: self.surface_size.width,
          height: self.surface_size.height,
          present_mode: self.surface_present_mode,
          alpha_mode: wgpu::CompositeAlphaMode::Opaque,
          view_formats: vec![],
        },
      );
      self.frame_color_texture = texture::Texture::create_frame_color_texture(
        &self.device,
        "frame_color_texture",
        self.surface_size.width,
        self.surface_size.height,
        MULTISAMPLE_SAMPLES,
      );
      self.frame_depth_texture = texture::Texture::create_frame_depth_texture(
        &self.device,
        "frame_depth_texture",
        self.surface_size.width,
        self.surface_size.height,
        MULTISAMPLE_SAMPLES,
      );
    }
  }

  fn input(&mut self, event: &WindowEvent) -> bool {
    match event {
      WindowEvent::KeyboardInput { input: KeyboardInput { virtual_keycode: Some(key), state, .. }, .. } => {
        self.camera_controller.process_keyboard(*key, *state)
      }
      WindowEvent::MouseWheel { delta, .. } => {
        self.camera_controller.process_scroll(delta);
        true
      }
      WindowEvent::MouseInput { button: MouseButton::Left, state, .. } => {
        self.mouse_pressed = *state == ElementState::Pressed;
        true
      }
      _ => false,
    }
  }

  fn update(&mut self, dt: std::time::Duration) {
    self.camera_controller.update_camera(&mut self.camera, dt);
    self.camera_uniform.update_view_proj(&self.camera, &self.projection);

    let old_position: cgmath::Vector3<_> = self.light_uniform.position.into();
    self.light_uniform.position =
      (cgmath::Quaternion::from_axis_angle((0.0, 1.0, 0.0).into(), cgmath::Deg(1.0)) * old_position).into();

    self.queue.write_buffer(&self.camera_buffer, 0, bytemuck::cast_slice(&[self.camera_uniform]));
    self.queue.write_buffer(&self.light_buffer, 0, bytemuck::cast_slice(&[self.light_uniform]));
  }

  fn render(&mut self) -> Result<(), wgpu::SurfaceError> {
    let surface_texture = self.surface.get_current_texture()?;
    let surface_texture_view = surface_texture.texture.create_view(&wgpu::TextureViewDescriptor::default());

    let mut encoder =
      self.device.create_command_encoder(&wgpu::CommandEncoderDescriptor { label: Some("Render Encoder") });

    {
      let mut render_pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
        label: Some("Render Pass"),
        color_attachments: &[Some(wgpu::RenderPassColorAttachment {
          view: &self.frame_color_texture.view,
          resolve_target: Some(&surface_texture_view),
          ops: wgpu::Operations {
            load: wgpu::LoadOp::Clear(wgpu::Color { r: 0.1, g: 0.2, b: 0.3, a: 1.0 }),
            store: wgpu::StoreOp::Store,
          },
        })],
        depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
          view: &self.frame_depth_texture.view,
          depth_ops: Some(wgpu::Operations { load: wgpu::LoadOp::Clear(1.0), store: wgpu::StoreOp::Store }),
          stencil_ops: None,
        }),
        occlusion_query_set: None,
        timestamp_writes: None,
      });

      render_pass.set_vertex_buffer(1, self.instance_buffer.slice(..));
      render_pass.set_pipeline(&self.light_render_pipeline);
      render_pass.draw_light_model(&self.obj_model, &self.camera_bind_group, &self.light_bind_group);

      render_pass.set_pipeline(&self.render_pipeline);
      render_pass.draw_model_instanced(
        &self.obj_model,
        0..self.instances.len() as u32,
        &self.camera_bind_group,
        &self.light_bind_group,
      );
    }
    self.queue.submit(iter::once(encoder.finish()));
    surface_texture.present();

    Ok(())
  }
}

pub async fn run() {
  env_logger::init();

  let event_loop = EventLoop::new();

  let mut state = State::new(&event_loop).await;
  let mut last_render_time = std::time::Instant::now();

  event_loop.run(move |event, _, control_flow| {
    *control_flow = ControlFlow::Poll;
    match event {
      Event::MainEventsCleared => state.window().request_redraw(),
      Event::DeviceEvent { event: DeviceEvent::MouseMotion { delta }, .. } => {
        if state.mouse_pressed {
          state.camera_controller.process_mouse(delta.0, delta.1)
        }
      }
      Event::WindowEvent { ref event, window_id } if window_id == state.window().id() && !state.input(event) => {
        match event {
          WindowEvent::CloseRequested
          | WindowEvent::KeyboardInput {
            input: KeyboardInput { state: ElementState::Pressed, virtual_keycode: Some(VirtualKeyCode::Escape), .. },
            ..
          } => *control_flow = ControlFlow::Exit,
          WindowEvent::Resized(physical_size) => {
            state.resize(*physical_size);
          }
          WindowEvent::ScaleFactorChanged { new_inner_size, .. } => {
            state.resize(**new_inner_size);
          }
          _ => {}
        }
      }
      Event::RedrawRequested(window_id) if window_id == state.window().id() => {
        let now = std::time::Instant::now();
        let dt = now - last_render_time;
        last_render_time = now;
        state.update(dt);
        match state.render() {
          Ok(_) => {}
          // Reconfigure the surface if it's lost or outdated
          Err(wgpu::SurfaceError::Lost | wgpu::SurfaceError::Outdated) => state.resize(state.surface_size),
          // The system is out of memory, we should probably quit
          Err(wgpu::SurfaceError::OutOfMemory) => *control_flow = ControlFlow::Exit,
          // We're ignoring timeouts
          Err(wgpu::SurfaceError::Timeout) => log::warn!("Surface timeout"),
        }
      }
      _ => {}
    }
  });
}
