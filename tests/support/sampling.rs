//! GPU sampling checks against analytic integrals, independent of scene appearance.
use anyhow::{Result, ensure};
use vxrt::renderer::Renderer;
const SOURCE: &str = concat!(
    include_str!("../../shaders/sampling.wgsl"),
    "\n",
    include_str!("sampling.wgsl")
);
pub async fn check(renderer: &Renderer) -> Result<()> {
    let device = &renderer.device;
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("sampling diagnostics"),
        source: wgpu::ShaderSource::Wgsl(SOURCE.into()),
    });
    let pipeline = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
        label: Some("sampling diagnostics"),
        layout: None,
        module: &shader,
        entry_point: Some("main"),
        compilation_options: Default::default(),
        cache: None,
    });
    const COUNT: u32 = 65536;
    let output = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("sampling diagnostics"),
        size: u64::from(COUNT) * 80,
        usage: wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::COPY_SRC,
        mapped_at_creation: false,
    });
    let group = device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: None,
        layout: &pipeline.get_bind_group_layout(0),
        entries: &[wgpu::BindGroupEntry {
            binding: 0,
            resource: output.as_entire_binding(),
        }],
    });
    let mut encoder = device.create_command_encoder(&Default::default());
    {
        let mut pass = encoder.begin_compute_pass(&Default::default());
        pass.set_pipeline(&pipeline);
        pass.set_bind_group(0, &group, &[]);
        pass.dispatch_workgroups(COUNT / 256, 1, 1);
    }
    renderer.queue.submit([encoder.finish()]);
    let bytes = renderer.read_buffer(&output, output.size())?;
    let mut sums = [0.0f64; 20];
    let mut square = 0.0;
    let mut product = 0.0;
    let mut distinct = [0usize; 3];
    for row in bytes.as_chunks::<80>().0 {
        let row: [f32; 20] =
            std::array::from_fn(|i| f32::from_ne_bytes(row[i * 4..i * 4 + 4].try_into().unwrap()));
        ensure!(
            row.iter().all(|x| x.is_finite()),
            "non-finite sampler output"
        );
        ensure!(
            row[0] > 0.0 && row[0] < 1.0 && row[1] > 0.0 && row[1] < 1.0,
            "random endpoint/out-of-range"
        );
        ensure!(
            row[2] >= 0.0 && row[2] <= 1.000001 && row[6] <= 0.0,
            "sample outside oriented hemisphere"
        );
        ensure!(
            (row[4] - 1.0).abs() < 1e-5 && (row[5] - 1.0).abs() < 1e-5,
            "non-unit sample direction"
        );
        ensure!(row[3] >= 0.05f32.cos() - 2e-7, "sample outside sun cone");
        ensure!(
            row[7] == 0.0 && row[8..12].iter().all(|x| *x >= 0.0),
            "negative energy or below-horizon light"
        );
        ensure!(
            row[16] == 0.0,
            "coherent bounce samples differ between pixels"
        );
        ensure!(
            row[17] != 0.0 && row[18] != 0.0 && row[19] != 0.0,
            "camera jitter, decorrelated bounces, or temporal sequence aliased"
        );
        for (sum, value) in sums.iter_mut().zip(row) {
            *sum += f64::from(value);
        }
        square += f64::from(row[0]).powi(2);
        product += f64::from(row[0]) * f64::from(row[1]);
        for j in 0..3 {
            distinct[j] += usize::from(row[12] != row[13 + j]);
        }
    }
    let mean = sums.map(|x| x / f64::from(COUNT));
    ensure!(
        (mean[0] - 0.5).abs() < 0.005 && (square / f64::from(COUNT) - 1.0 / 3.0).abs() < 0.005,
        "uniform moments"
    );
    ensure!(
        (product / f64::from(COUNT) - mean[0] * mean[1]).abs() < 0.002,
        "correlated dimensions"
    );
    ensure!(
        (mean[2] - 2.0 / 3.0).abs() < 0.005,
        "cosine hemisphere moment"
    );
    ensure!(
        (mean[3] - (1.0 + 0.05f64.cos()) / 2.0).abs() < 1e-5,
        "uniform cone solid-angle moment"
    );
    let sun = 0.05f64.sin().powi(2);
    let horizon = (0.05 - 0.1f64.sin() / 2.0) / std::f64::consts::PI;
    for (actual, expected) in [
        (mean[8], sun),
        (mean[9], 1.0),
        (mean[10], sun * 0.5),
        (mean[11], horizon),
    ] {
        ensure!(
            (actual - expected).abs() < expected * 0.02,
            "MIS integral: measured {actual}, expected {expected}"
        );
    }
    ensure!(
        distinct.iter().all(|&n| n > 65000),
        "pixel/seed/integer sample-index aliasing"
    );
    println!(
        "Sampler checks passed: hemisphere/cone distributions, independent dimensions, MIS energy (aligned, tilted, horizon, constant environment)."
    );
    Ok(())
}
#[cfg(test)]
mod tests {
    #[test]
    fn validate_diagnostics() {
        let module = naga::front::wgsl::parse_str(super::SOURCE).unwrap();
        naga::valid::Validator::new(
            naga::valid::ValidationFlags::all(),
            naga::valid::Capabilities::all(),
        )
        .validate(&module)
        .unwrap();
    }
}
