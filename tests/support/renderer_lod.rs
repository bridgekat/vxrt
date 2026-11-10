//! GPU regression with private tree access, avoiding a test-only public mutation API.
use super::*;
#[test]
#[ignore = "requires a graphics adapter"]
fn resident_lod_matches_derefined_summary() -> Result<()> {
    pollster::block_on(async {
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_without_display_handle());
        let adapter = instance.request_adapter(&Default::default()).await?;
        for path in [false, true] {
            let config = Config {
                world_levels: 3,
                lod_quality: 0.001,
                ..Default::default()
            };
            let mut r =
                Renderer::new(&adapter, config, 33, 19, wgpu::TextureFormat::Rgba8Unorm).await?;
            r.replace_packed_scene(&[9, 5, 3, 7, 3, 7, 3, 7, 3, 7])?;
            let root = r.streaming.tree.root_node();
            ensure!(
                r.streaming.tree.children(root).is_some(),
                "test requires resident detail"
            );
            let summary = r.streaming.tree.node_data(root);
            let camera = Vector3::new(4.0, 12.0, 12.0);
            r.render(camera, [-45.0, 0.0], path, false, 0, None)?;
            let resident = r.read_frame()?;
            r.check_tree()?;
            ensure!(r.streaming.tree.derefine(root));
            ensure!(r.streaming.tree.node_data(root) == summary);
            r.samples = 0;
            r.render(camera, [-45.0, 0.0], path, false, 0, None)?;
            let unloaded = r.read_frame()?;
            ensure!(
                resident == unloaded,
                "LOD differs after dropping children (path={path})"
            );
            r.check_tree()?;
            // Consume any interactive feedback; neither branch LOD nor its unloaded
            // summary should request detail below the selected screen-space LOD.
            r.render(camera, [-45.0, 0.0], path, false, 1, None)?;
            ensure!(r.streaming_stats().requested == 0);
            r.replace_packed_scene(&[1, 3])?;
            r.render(camera, [-45.0, 0.0], path, false, 0, None)?;
            ensure!(
                resident != r.read_frame()?,
                "LOD comparison must include visible geometry"
            );
        }
        Ok(())
    })
}
