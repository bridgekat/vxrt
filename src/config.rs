use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(default, deny_unknown_fields)]
pub struct Config {
    pub world_levels: u32,
    pub max_height: u32,
    pub dynamic: bool,
    pub max_nodes: u32,
    pub noise_levels: u32,
    pub partial_levels: u32,
    pub lod_quality: f32,
    pub field_of_view: f32,
    pub render_width: u32,
    pub render_height: u32,
    pub seed: u32,
    pub coherent_sampling: bool,
}
impl Default for Config {
    fn default() -> Self {
        Self {
            world_levels: 8,
            max_height: 256,
            dynamic: false,
            max_nodes: 16_777_216,
            noise_levels: 8,
            partial_levels: 4,
            lod_quality: 0.5,
            field_of_view: 70.0,
            render_width: 0,
            render_height: 0,
            seed: 1,
            coherent_sampling: true,
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
            (1..=20).contains(&self.world_levels),
            "world_levels must be 1..=20 (traversal stack capacity)"
        );
        ensure!(
            (1..=12).contains(&self.noise_levels),
            "noise_levels must be 1..=12"
        );
        ensure!(
            self.partial_levels <= 8
                && self.noise_levels <= self.world_levels + self.partial_levels,
            "invalid noise/partial levels"
        );
        ensure!(
            self.max_nodes >= 9 && self.max_nodes < (1 << 30),
            "max_nodes must be 9..2^30"
        );
        ensure!(
            self.dynamic || self.world_levels <= 10,
            "static generation is limited to 10 levels; use dynamic for larger worlds"
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

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_invalid_shader_parameters() {
        for config in [
            Config {
                world_levels: 21,
                ..Default::default()
            },
            Config {
                partial_levels: u32::MAX,
                ..Default::default()
            },
            Config {
                max_nodes: 8,
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
    }
}
