mod app;
mod config;
mod renderer;
mod sampling_checks;
mod shaders;
mod smoke;
mod world;
fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "vxrt=info,wgpu_core=warn,wgpu_hal=warn".into()),
        )
        .init();
    let args: Vec<_> = std::env::args().skip(1).collect();
    match args.as_slice() {
        [] => app::run(config::Config::load()?),
        [arg] if arg == "--smoke-test" => pollster::block_on(smoke::run()),
        [arg] if arg == "--print-config" => {
            println!("{}", toml::to_string_pretty(&config::Config::default())?);
            Ok(())
        }
        _ => anyhow::bail!("usage: vxrt [--smoke-test | --print-config]"),
    }
}
#[cfg(test)]
mod shader_tests {
    #[test]
    fn validate_shaders() {
        for source in [crate::shaders::TRACE, crate::shaders::PRESENT] {
            let module = naga::front::wgsl::parse_str(source)
                .unwrap_or_else(|e| panic!("{}", e.emit_to_string(source)));
            naga::valid::Validator::new(
                naga::valid::ValidationFlags::all(),
                naga::valid::Capabilities::all(),
            )
            .validate(&module)
            .unwrap();
        }
    }
}
