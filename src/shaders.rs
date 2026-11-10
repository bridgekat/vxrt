// GPU programs embedded in the executable; sampling helpers are shared with diagnostics.
pub const TRACE: &str = concat!(
    include_str!("../shaders/sampling.wgsl"),
    "\n",
    include_str!("../shaders/main.wgsl")
);
pub const PRESENT: &str = include_str!("../shaders/present.wgsl");
pub const SCATTER: &str = include_str!("../shaders/scatter.wgsl");
pub const REQUESTS: &str = include_str!("../shaders/requests.wgsl");
