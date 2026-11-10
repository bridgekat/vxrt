use anyhow::{Context, Result};
use cgmath::{Deg, Matrix3, Vector3};
use std::{
    collections::HashSet,
    sync::Arc,
    time::{Duration, Instant},
};
use vxrt::{
    config::Config,
    core::tree::TreeStats,
    renderer::Renderer,
    stats_overlay::{OverlayStats, StatsOverlay},
};
use winit::{
    application::ApplicationHandler,
    event::{DeviceEvent, ElementState, WindowEvent},
    event_loop::{ActiveEventLoop, EventLoop},
    keyboard::{KeyCode, PhysicalKey},
    window::{CursorGrabMode, Window, WindowId},
};

pub fn run(config: Config) -> Result<()> {
    let event_loop = EventLoop::new()?;
    let mut app = App {
        config,
        state: None,
        error: None,
    };
    event_loop.run_app(&mut app)?;
    if let Some(error) = app.error {
        return Err(error);
    }
    Ok(())
}
struct App {
    config: Config,
    state: Option<State>,
    error: Option<anyhow::Error>,
}
struct State {
    window: Arc<Window>,
    surface: wgpu::Surface<'static>,
    surface_config: wgpu::SurfaceConfiguration,
    renderer: Renderer,
    overlay: StatsOverlay,
    egui_input: egui_winit::State,
    tree_stats: TreeStats,
    stats_time: Instant,
    fps: f32,
    position: Vector3<f32>,
    velocity: Vector3<f32>,
    rotation: [f32; 2],
    keys: HashSet<KeyCode>,
    path: bool,
    locked: bool,
    focused: bool,
    last_tick: Instant,
    start: Instant,
    title_time: Instant,
    frames: u32,
    minimized: bool,
}
impl State {
    fn new(event_loop: &ActiveEventLoop, config: Config) -> Result<Self> {
        let window = Arc::new(
            event_loop.create_window(
                Window::default_attributes()
                    .with_title("Voxel Raycasting Test")
                    .with_inner_size(winit::dpi::LogicalSize::new(852, 480)),
            )?,
        );
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_without_display_handle());
        let surface = instance.create_surface(window.clone())?;
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            compatible_surface: Some(&surface),
            force_fallback_adapter: false,
        }))?;
        let size = window.inner_size();
        let caps = surface.get_capabilities(&adapter);
        // The presentation shader performs pow(1/2.2), so use a non-sRGB view.
        let format = caps
            .formats
            .iter()
            .copied()
            .find(|f| !f.is_srgb())
            .unwrap_or(caps.formats[0]);
        let view_format = format.remove_srgb_suffix();
        let surface_config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format,
            width: size.width.max(1),
            height: size.height.max(1),
            present_mode: wgpu::PresentMode::AutoNoVsync,
            desired_maximum_frame_latency: 2,
            alpha_mode: caps.alpha_modes[0],
            view_formats: vec![view_format],
        };
        let renderer = pollster::block_on(Renderer::new(
            &adapter,
            config.clone(),
            size.width,
            size.height,
            view_format,
        ))?;
        surface.configure(&renderer.device, &surface_config);
        let overlay = StatsOverlay::new(&renderer.device, view_format);
        let egui_input = egui_winit::State::new(
            overlay.context.clone(),
            egui::ViewportId::ROOT,
            window.as_ref(),
            Some(window.scale_factor() as f32),
            None,
            None,
        );
        let tree_stats = renderer.tree_stats();
        let world = (1u32 << config.world_levels) as f32;
        let mut state = Self {
            window,
            surface,
            surface_config,
            renderer,
            overlay,
            egui_input,
            tree_stats,
            stats_time: Instant::now(),
            fps: 0.,
            position: Vector3::new(world * 0.5, world + 2.0, world * 0.5),
            velocity: Vector3::new(0.0, 0.0, 0.0),
            rotation: [0.0; 2],
            keys: HashSet::new(),
            path: false,
            locked: false,
            focused: true,
            last_tick: Instant::now(),
            start: Instant::now(),
            title_time: Instant::now(),
            frames: 0,
            minimized: false,
        };
        state.lock(true);
        Ok(state)
    }
    fn lock(&mut self, lock: bool) {
        let result = if lock {
            self.window
                .set_cursor_grab(CursorGrabMode::Locked)
                .or_else(|_| self.window.set_cursor_grab(CursorGrabMode::Confined))
        } else {
            self.window.set_cursor_grab(CursorGrabMode::None)
        };
        self.locked = lock && result.is_ok();
        self.window.set_cursor_visible(!self.locked);
    }
    fn tick(&mut self) {
        let step = Duration::from_secs_f64(1.0 / 30.0);
        let now = Instant::now();
        if self.path || !self.focused {
            self.last_tick = now;
            return;
        }
        // Avoid a long catch-up loop after dragging/minimizing the window.
        if now.duration_since(self.last_tick) > Duration::from_millis(250) {
            self.last_tick = now - step;
        }
        while now.duration_since(self.last_tick) >= step {
            self.position += self.velocity;
            self.velocity *= 0.8;
            let key = |k| f32::from(self.keys.contains(&k));
            let mut acc = Vector3::new(
                key(KeyCode::KeyD) - key(KeyCode::KeyA),
                key(KeyCode::Space) - key(KeyCode::ControlLeft),
                key(KeyCode::KeyS) - key(KeyCode::KeyW),
            );
            if self.keys.contains(&KeyCode::Tab) {
                acc *= 256.0;
            }
            self.velocity += Matrix3::from_angle_y(Deg(self.rotation[1])) * (acc * 0.1);
            self.last_tick += step;
        }
    }
    fn draw(&mut self) -> Result<()> {
        if self.minimized {
            return Ok(());
        }
        self.tick();
        let texture = match self.surface.get_current_texture() {
            wgpu::CurrentSurfaceTexture::Success(t)
            | wgpu::CurrentSurfaceTexture::Suboptimal(t) => t,
            wgpu::CurrentSurfaceTexture::Lost | wgpu::CurrentSurfaceTexture::Outdated => {
                self.surface
                    .configure(&self.renderer.device, &self.surface_config);
                return Ok(());
            }
            wgpu::CurrentSurfaceTexture::Timeout | wgpu::CurrentSurfaceTexture::Occluded => {
                return Ok(());
            }
            other => anyhow::bail!("surface acquisition failed: {other:?}"),
        };
        let view = texture.texture.create_view(&wgpu::TextureViewDescriptor {
            format: Some(self.surface_config.format.remove_srgb_suffix()),
            ..Default::default()
        });
        let delta = (self.last_tick.elapsed().as_secs_f32() * 30.0).min(1.0);
        let position = if self.path {
            self.position
        } else {
            self.position + self.velocity * delta
        };
        self.renderer.render(
            position,
            self.rotation,
            self.path,
            self.keys.contains(&KeyCode::KeyM),
            self.renderer.samples,
            Some(&view),
        )?;
        if self.stats_time.elapsed() >= Duration::from_millis(250) {
            self.tree_stats = self.renderer.tree_stats();
            self.stats_time = Instant::now();
        }
        let stats = OverlayStats {
            tree: self.tree_stats,
            streaming: self.renderer.streaming_stats(),
            path_tracing: self.path,
            samples: self.renderer.samples,
            fps: self.fps,
            position: position.into(),
            feedback_pending: self.renderer.feedback_pending(),
        };
        let input = self.egui_input.take_egui_input(&self.window);
        let output = self.overlay.draw(
            &self.renderer,
            &view,
            input,
            [self.surface_config.width, self.surface_config.height],
            &stats,
        );
        if !self.locked {
            self.egui_input.handle_platform_output(&self.window, output);
        }
        texture.present();
        self.frames += 1;
        if self.title_time.elapsed() >= Duration::from_secs(1) {
            self.fps = self.frames as f32 / self.title_time.elapsed().as_secs_f32();
            let stats = if self.path {
                format!("{} spp", self.renderer.samples)
            } else {
                format!(
                    "{:.0} FPS | X {:.1} Y {:.1} Z {:.1}",
                    self.frames as f32 / self.title_time.elapsed().as_secs_f32(),
                    self.position.x,
                    self.position.y,
                    self.position.z
                )
            };
            self.window
                .set_title(&format!("Voxel Raycasting Test — {stats}"));
            self.frames = 0;
            self.title_time = Instant::now();
        }
        Ok(())
    }
}
impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.state.is_none() {
            match State::new(event_loop, self.config.clone()) {
                Ok(s) => self.state = Some(s),
                Err(e) => {
                    self.error = Some(e);
                    event_loop.exit();
                }
            }
        }
    }
    fn window_event(&mut self, event_loop: &ActiveEventLoop, _id: WindowId, event: WindowEvent) {
        let Some(s) = &mut self.state else {
            return;
        };
        let _ = s.egui_input.on_window_event(&s.window, &event);
        let result: Result<()> = (|| {
            match event {
                WindowEvent::CloseRequested => event_loop.exit(),
                WindowEvent::Focused(focused) => {
                    s.focused = focused;
                    if !focused {
                        s.keys.clear();
                        s.lock(false);
                    } else if !s.path {
                        s.lock(true);
                    }
                }
                WindowEvent::Resized(size) => {
                    s.minimized = size.width == 0 || size.height == 0;
                    if !s.minimized {
                        s.surface_config.width = size.width;
                        s.surface_config.height = size.height;
                        s.surface.configure(&s.renderer.device, &s.surface_config);
                        s.renderer.resize(size.width, size.height)?;
                    }
                }
                WindowEvent::KeyboardInput { event, .. } => {
                    if let PhysicalKey::Code(key) = event.physical_key {
                        if event.state == ElementState::Released {
                            s.keys.remove(&key);
                        } else if s.keys.insert(key) && !event.repeat {
                            match key {
                                KeyCode::Escape => event_loop.exit(),
                                KeyCode::KeyP => {
                                    s.path = !s.path;
                                    s.renderer.samples = 0;
                                    s.position += s.velocity
                                        * (s.last_tick.elapsed().as_secs_f32() * 30.0).min(1.0);
                                    s.velocity = Vector3::new(0.0, 0.0, 0.0);
                                    s.lock(!s.path);
                                }
                                KeyCode::KeyL => s.lock(!s.locked),
                                KeyCode::KeyC => {
                                    let (allocated, reachable) =
                                        s.renderer.check_tree().context("octree check")?;
                                    tracing::info!(allocated, reachable, stats=?s.renderer.tree_stats(), streaming=?s.renderer.streaming_stats(), "octree checked");
                                }
                                KeyCode::KeyG => s.renderer.compact_tree()?,
                                KeyCode::KeyO => {
                                    std::fs::create_dir_all("screenshots")?;
                                    let name = format!(
                                        "screenshots/{}x{}-{}spp-{}.png",
                                        s.renderer.width,
                                        s.renderer.height,
                                        s.renderer.samples,
                                        s.start.elapsed().as_millis()
                                    );
                                    s.renderer.save_png(&name)?;
                                    tracing::info!(path = name, "saved screenshot");
                                }
                                _ => {}
                            }
                        }
                    }
                }
                WindowEvent::RedrawRequested => s.draw()?,
                _ => {}
            }
            Ok(())
        })();
        if let Err(e) = result {
            self.error = Some(e);
            event_loop.exit();
        }
    }
    fn device_event(&mut self, _: &ActiveEventLoop, _: winit::event::DeviceId, event: DeviceEvent) {
        if let Some(s) = &mut self.state
            && s.locked
            && !s.path
            && s.focused
            && let DeviceEvent::MouseMotion { delta } = event
        {
            s.rotation[0] -= delta.1 as f32 * 0.3;
            s.rotation[1] -= delta.0 as f32 * 0.3;
        }
    }
    fn about_to_wait(&mut self, _: &ActiveEventLoop) {
        if let Some(s) = &self.state
            && !s.minimized
        {
            s.window.request_redraw();
        }
    }
}
