use anyhow::*;
use image::GenericImageView;

pub struct Texture {
  pub texture: wgpu::Texture,
  pub view: wgpu::TextureView,
  pub sampler: wgpu::Sampler,
}

impl Texture {
  pub const FRAME_COLOR_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Bgra8Unorm;
  pub const FRAME_DEPTH_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Depth32Float;

  pub fn create_frame_color_texture(
    device: &wgpu::Device,
    label: &str,
    width: u32,
    height: u32,
    sample_count: u32,
  ) -> Self {
    let texture = device.create_texture(&wgpu::TextureDescriptor {
      label: Some(label),
      size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
      mip_level_count: 1,
      sample_count,
      dimension: wgpu::TextureDimension::D2,
      format: Self::FRAME_COLOR_FORMAT,
      usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING,
      view_formats: &[Self::FRAME_COLOR_FORMAT],
    });
    let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
    let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
      address_mode_u: wgpu::AddressMode::ClampToEdge,
      address_mode_v: wgpu::AddressMode::ClampToEdge,
      mag_filter: wgpu::FilterMode::Linear,
      min_filter: wgpu::FilterMode::Linear,
      mipmap_filter: wgpu::FilterMode::Nearest,
      lod_min_clamp: 0.0,
      lod_max_clamp: 100.0,
      ..Default::default()
    });
    Self { texture, view, sampler }
  }

  pub fn create_frame_depth_texture(
    device: &wgpu::Device,
    label: &str,
    width: u32,
    height: u32,
    sample_count: u32,
  ) -> Self {
    let texture = device.create_texture(&wgpu::TextureDescriptor {
      label: Some(label),
      size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
      mip_level_count: 1,
      sample_count,
      dimension: wgpu::TextureDimension::D2,
      format: Self::FRAME_DEPTH_FORMAT,
      usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING,
      view_formats: &[Self::FRAME_DEPTH_FORMAT],
    });
    let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
    let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
      address_mode_u: wgpu::AddressMode::ClampToEdge,
      address_mode_v: wgpu::AddressMode::ClampToEdge,
      mag_filter: wgpu::FilterMode::Linear,
      min_filter: wgpu::FilterMode::Linear,
      mipmap_filter: wgpu::FilterMode::Nearest,
      compare: Some(wgpu::CompareFunction::LessEqual),
      lod_min_clamp: 0.0,
      lod_max_clamp: 100.0,
      ..Default::default()
    });
    Self { texture, view, sampler }
  }

  #[allow(dead_code)]
  pub fn from_bytes(
    device: &wgpu::Device,
    queue: &wgpu::Queue,
    bytes: &[u8],
    label: &str,
    is_normal_map: bool,
  ) -> Result<Self> {
    let img = image::load_from_memory(bytes)?;
    Self::from_image(device, queue, &img, Some(label), is_normal_map)
  }

  pub fn from_image(
    device: &wgpu::Device,
    queue: &wgpu::Queue,
    img: &image::DynamicImage,
    label: Option<&str>,
    is_normal_map: bool,
  ) -> Result<Self> {
    let dimensions = img.dimensions();
    let rgba = img.to_rgba8();

    let size = wgpu::Extent3d { width: dimensions.0, height: dimensions.1, depth_or_array_layers: 1 };
    let format = if is_normal_map { wgpu::TextureFormat::Rgba8Unorm } else { wgpu::TextureFormat::Rgba8UnormSrgb };
    let texture = device.create_texture(&wgpu::TextureDescriptor {
      label,
      size,
      mip_level_count: 1,
      sample_count: 1,
      dimension: wgpu::TextureDimension::D2,
      format,
      usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
      view_formats: &[],
    });

    queue.write_texture(
      wgpu::ImageCopyTexture {
        aspect: wgpu::TextureAspect::All,
        texture: &texture,
        mip_level: 0,
        origin: wgpu::Origin3d::ZERO,
      },
      &rgba,
      wgpu::ImageDataLayout { offset: 0, bytes_per_row: Some(4 * dimensions.0), rows_per_image: Some(dimensions.1) },
      size,
    );

    let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
    let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
      address_mode_u: wgpu::AddressMode::ClampToEdge,
      address_mode_v: wgpu::AddressMode::ClampToEdge,
      address_mode_w: wgpu::AddressMode::ClampToEdge,
      mag_filter: wgpu::FilterMode::Linear,
      min_filter: wgpu::FilterMode::Nearest,
      mipmap_filter: wgpu::FilterMode::Nearest,
      ..Default::default()
    });

    Ok(Self { texture, view, sampler })
  }
}
