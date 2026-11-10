use crate::core::MAX_WORLD_LEVELS;
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(default, deny_unknown_fields)]
pub struct Config {
    pub world_levels: u32,
    pub max_height: u32,
    pub lod_quality: f32,
    pub field_of_view: f32,
    pub render_width: u32,
    pub render_height: u32,
    pub seed: u32,
    pub coherent_sampling: bool,
    pub sky_intensity: f32,
    pub sun_intensity: f32,
    pub march_budget: u32,
    pub splits_per_frame: u32,
    pub max_split_requests: u32,
}
impl Default for Config {
    fn default() -> Self {
        Self {
            world_levels: 10,
            max_height: 256,
            lod_quality: 1.0,
            field_of_view: 90.0,
            render_width: 0,
            render_height: 0,
            seed: 1,
            coherent_sampling: true,
            sky_intensity: 1.0,
            sun_intensity: 256.0,
            march_budget: 1024,
            splits_per_frame: 1024,
            max_split_requests: 16384,
        }
    }
}
impl Config {
    pub fn load() -> Result<Self> {
        let path = std::path::Path::new("config.toml");
        let config: Self = if path.exists() {
            toml::from_str(&std::fs::read_to_string(path)?).context("invalid config.toml")?
        } else {
            Self::default()
        };
        config.validate()?;
        Ok(config)
    }
    pub fn validate(&self) -> Result<()> {
        ensure!(
            (1..=1048576).contains(&self.max_split_requests),
            "max_split_requests must be 1..=1048576"
        );
        ensure!(
            (1..=4096).contains(&self.splits_per_frame),
            "splits_per_frame must be 1..=4096"
        );
        ensure!(
            (1..=65536).contains(&self.march_budget),
            "march_budget must be 1..=65536"
        );
        ensure!(
            self.sky_intensity.is_finite()
                && self.sky_intensity >= 0.0
                && self.sun_intensity.is_finite()
                && self.sun_intensity >= 0.0,
            "sky_intensity and sun_intensity must be finite and nonnegative"
        );
        ensure!(
            (1..=MAX_WORLD_LEVELS).contains(&self.world_levels),
            "world_levels must be 1..={MAX_WORLD_LEVELS}"
        );
        ensure!(
            self.lod_quality.is_finite() && self.lod_quality > 0.0,
            "lod_quality must be positive"
        );
        ensure!(
            (1.0..179.0).contains(&self.field_of_view),
            "field_of_view must be 1..179 degrees"
        );
        Ok(())
    }
}
