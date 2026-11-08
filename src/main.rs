mod app;
use vxrt::config;
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
        [arg] if arg == "--print-config" => {
            println!("{}", toml::to_string_pretty(&config::Config::default())?);
            Ok(())
        }
        _ => anyhow::bail!("usage: vxrt [--print-config]"),
    }
}
