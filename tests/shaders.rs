#[test]
fn validate_shaders() {
    for source in [
        vxrt::shaders::TRACE,
        vxrt::shaders::PRESENT,
        vxrt::shaders::SCATTER,
    ] {
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
