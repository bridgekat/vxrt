use vxrt::config::*;
#[test]
fn rejects_invalid_shader_parameters() {
    for config in [
        Config {
            march_budget: 0,
            ..Default::default()
        },
        Config {
            march_budget: 65537,
            ..Default::default()
        },
        Config {
            sky_intensity: -1.0,
            ..Default::default()
        },
        Config {
            sun_intensity: f32::NAN,
            ..Default::default()
        },
        Config {
            world_levels: 11,
            ..Default::default()
        },
        Config {
            lod_quality: f32::NAN,
            ..Default::default()
        },
        Config {
            field_of_view: 180.0,
            ..Default::default()
        },
    ] {
        assert!(config.validate().is_err());
    }
    Config::default().validate().unwrap();
    for march_budget in [1, 1024, 65536] {
        let config: Config = toml::from_str(&format!("march_budget = {march_budget}")).unwrap();
        assert_eq!(config.march_budget, march_budget);
        config.validate().unwrap();
    }
    Config {
        sky_intensity: 0.0,
        sun_intensity: 0.0,
        ..Default::default()
    }
    .validate()
    .unwrap();
}
