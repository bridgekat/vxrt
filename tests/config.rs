use vxrt::config::*;
#[test]
fn rejects_invalid_shader_parameters() {
    for config in [
        Config {
            max_split_requests: 0,
            ..Default::default()
        },
        Config {
            max_split_requests: 1048577,
            ..Default::default()
        },
        Config {
            splits_per_frame: 0,
            ..Default::default()
        },
        Config {
            splits_per_frame: 4097,
            ..Default::default()
        },
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
            world_levels: 13,
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
    for levels in [1, 10, 11, 12] {
        let config: Config = toml::from_str(&format!("world_levels = {levels}")).unwrap();
        assert_eq!(config.world_levels, levels);
        config.validate().unwrap();
    }
    for splits in [1, 1024, 4096] {
        let config: Config = toml::from_str(&format!("splits_per_frame = {splits}")).unwrap();
        assert_eq!(config.splits_per_frame, splits);
        config.validate().unwrap();
    }
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
