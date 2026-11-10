//! CPU-only terrain, local node operations, pools, and runtime tree management.
//! Packed scene import is separate from the live pool representation.
pub mod node;
pub mod packed;
pub mod pool;
pub mod residency;
pub mod terrain;
pub mod tree;
/// Maximum supported octree depth (world side length is 2^depth).
pub const MAX_WORLD_LEVELS: u32 = 12;

// Crate-private synchronization regression tests live with the test sources.
#[cfg(test)]
#[path = "../../tests/support/tree_feedback.rs"]
mod feedback_tests;
