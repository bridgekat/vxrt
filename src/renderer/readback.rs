//! Synchronous GPU readback and image export.
use super::Renderer;
use anyhow::Result;

impl Renderer {
    pub fn read_frame(&self) -> Result<Vec<[f32; 4]>> {
        let bytes = self.read_buffer(&self.frame, self.frame.size())?;
        Ok(bytes
            .as_chunks::<16>()
            .0
            .iter()
            .map(|p| {
                std::array::from_fn(|i| f32::from_ne_bytes(p[i * 4..i * 4 + 4].try_into().unwrap()))
            })
            .collect())
    }
    pub fn save_png(&self, path: impl AsRef<std::path::Path>) -> Result<()> {
        let pixels = self.read_frame()?;
        let image = image::RgbImage::from_fn(self.width, self.height, |x, y| {
            let pixel = pixels[((self.height - 1 - y) * self.width + x) as usize];
            image::Rgb(std::array::from_fn(|c| {
                (pixel[c].max(0.0).powf(1.0 / 2.2).min(1.0) * 255.0).round() as u8
            }))
        });
        image.save(path)?;
        Ok(())
    }
    pub fn read_buffer(&self, source: &wgpu::Buffer, size: u64) -> Result<Vec<u8>> {
        let buffer = self.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("readback"),
            size,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let mut encoder = self.device.create_command_encoder(&Default::default());
        encoder.copy_buffer_to_buffer(source, 0, &buffer, 0, size);
        self.queue.submit([encoder.finish()]);
        let (tx, rx) = std::sync::mpsc::channel();
        buffer
            .slice(..)
            .map_async(wgpu::MapMode::Read, move |result| {
                let _ = tx.send(result);
            });
        self.device.poll(wgpu::PollType::wait_indefinitely())?;
        rx.recv()??;
        let result = buffer.slice(..).get_mapped_range().to_vec();
        buffer.unmap();
        Ok(result)
    }
}
