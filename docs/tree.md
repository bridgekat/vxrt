# Sparse voxel tree design

The renderer uses a CPU-owned sparse voxel octree (SVO). GPU rays read its resident representation and mark leaves that need refinement. The CPU generates children from baked terrain bounds, merges compatible leaves, and uploads changed slots. GPU shaders never allocate nodes or modify topology.

Interactive rendering permits refinement. Accumulating path tracing freezes the resident tree and camera; missing detail remains a coarse solid preview in both modes. Generated detail stays resident: there is currently no unloading or eviction.

## Module boundaries

| Module | Responsibility |
| --- | --- |
| `src/core/terrain.rs` | Height function and immutable aligned min/max pyramid |
| `src/core/node.rs` | Spatial regions, occupancy classification, local split and merge rules |
| `src/core/pool.rs` | Stable slot indices, freelist allocation, doubling growth, feedback invalidation |
| `src/core/tree.rs` | Root, parent links, topology changes, dirty slots, statistics, validation |
| `src/core/packed.rs` | Packed scene validation and independent exact reference generation |
| `src/renderer/streaming.rs` | Feedback readback, refinement scheduling, GPU buffers, scatter uploads |
| `shaders/main.wgsl` | Read-only traversal and atomic refinement requests |
| `shaders/scatter.wgsl` | Applying complete slot patches |

`core` has no windowing or GPU dependencies. One CPU owner currently performs tree operations on the render thread. The separation permits future worker scheduling, but there are no concurrent pool mutations or allocator locks today.

## Terrain bounds and local operations

The world is a cube of side `2^world_levels`. Startup bakes a height field clipped to the world and the exclusive `max_height` cutoff. A pyramid stores minimum and maximum heights for every aligned `2^n × 2^n` XZ square. Each level reduces four squares, giving approximately `4/3 × side²` min/max pairs across all levels. Runtime refinement reads these bounds rather than evaluating terrain again.

For a cubic region with vertical interval `[y, y + size)` and height bounds `[min, max]`:

- `y >= max` proves the region empty.
- `y + size <= min` proves it solid.
- Otherwise it is mixed and needs finer detail.

A mixed region is represented by a deferred leaf with solid occupancy. Splitting classifies its eight child regions independently. Child index bits 0, 1, and 2 select the upper half of X, Y, and Z respectively.

Eight children can merge only when they are all exact leaves with equal occupancy. Deferred solid previews are not compatible with exact solid leaves or with one another for merging: their apparent solidity is an approximation. `node::merge` owns this local rule; `Tree::compact` applies it bottom-up and frees redundant descendants. Future material fields must participate in compatibility before richer leaves can merge safely.

## Runtime representation and allocation

Branches and leaves use separate pools so growing leaf material data does not inflate every branch.

| GPU record | Size | Contents |
| --- | --- | --- |
| Branch | 32 bytes | Eight 32-bit child descriptors |
| Leaf | 16 bytes | Occupancy, deferred-detail flag, two reserved words |
| Request | 4 bytes per leaf slot | Atomic boolean refinement flag |

Descriptors store the pool index in the upper bits and a tag in the low two bits: branch `1`, leaf `3`. The root descriptor is passed through the scene uniform. GPU pools have no packed-tree header. CPU records additionally retain spatial regions and parent links; these are not uploaded.

Each pool begins with 16 slots. Allocation reuses freelist entries; when none remain, capacity doubles within a limit derived from adapter buffer limits. Live slots never move during growth or merging. Freeing a slot makes it reusable without shifting other allocations, so external fragmentation does not require compaction. Reserved capacity remains allocated after merging; unused capacity and CPU metadata still consume memory.

A handle is a slot index, not a permanent identity. After deallocation the same index can describe another node. There are no per-slot generation counters. Delayed GPU feedback is protected by the snapshot invariant below.

## Topology changes

A split first checks that one branch and eight leaves can be allocated. If capacity is insufficient, it leaves the original deferred preview intact. Otherwise it:

1. Classifies and allocates eight child leaves and their parent branch.
2. Replaces the old leaf reference in its parent, or replaces the root.
3. Frees the old leaf and marks all changed live records dirty.

A merge frees the redundant subtree, allocates the replacement exact leaf, and updates its parent or the root. Freed GPU records need not be cleared because the uploaded topology no longer references them. Dirty sets deduplicate records and upload only their final live contents; a freed slot that is reused before synchronization produces one patch for its new allocation.

Automatic compaction runs after a batch with successful splits, avoiding a whole-tree merge scan on idle frames. The G control also requests compaction in interactive mode. Path tracing pauses both operations.

## GPU-visible state and delayed feedback

Two independent kinds of CPU bookkeeping are required:

| Bookkeeping | Meaning | Lifetime |
| --- | --- | --- |
| Dirty slot sets | Final live records not yet included in an upload batch | Drained by upload synchronization |
| Feedback invalidation sets | Slot identities changed since the GPU snapshot producing the outstanding readback | Cleared only after that batch is consumed or discarded |

At most one readback batch is outstanding. After uploads and traversal have been recorded, capturing its request flags starts an empty invalidation set in each pool. The tracked snapshot is the state those ordered GPU commands will observe; it need not have finished executing when capture is recorded.

While that batch is outstanding, freeing or allocating a slot inserts its index into the corresponding invalidation set. Pool growth preserves existing identities. A returned leaf index is accepted only if it is still allocated and absent from the invalidation set. Its current refinement eligibility is then checked again. In-place content edits do not by themselves change identity.

The invariant is: **every accepted request still refers to the allocation that occupied its index in the GPU state that generated that request.** Logs remain active while processing the batch, because one split can free a slot that another split immediately reuses.

Upload synchronization must not clear these logs. A newer upload may complete before feedback from an older snapshot is consumed. Command ordering protects GPU memory access, but cannot identify a reused CPU slot in an old readback. The invalidation sets supply that identity check without generation fields. Supporting multiple outstanding snapshots would require separate snapshot tracking or another identity scheme.

Scene replacement discards outstanding feedback. During accumulation, completed batches are counted and discarded without topology changes. New refinement requests are disabled while path tracing.

## Frame ordering and scattered uploads

The renderer uses this order:

1. Poll for a completed feedback batch without blocking. If interactive, process requests up to `splits_per_frame` successful splits and compact when needed.
2. Record GPU pool growth copies, then scatter patches for dirty branch and leaf slots. Rebuild bindings when buffers change.
3. Clear request flags and provide the current root and frame parameters.
4. Run traversal: interactive beam pass followed by full-resolution rays, or the path-tracing pass.
5. If interactive and no readback is outstanding, copy request flags into a readback buffer. Submit commands and request asynchronous mapping.

Copies and separate compute passes are ordered through wgpu command submission. Traversal therefore sees complete topology updates, and readback copies the flags produced after that frame's clear. CPU code reads a feedback buffer only after its mapping callback succeeds. No shader workgroup barrier is used as a substitute for ordering between dispatches.

Each nonempty pool update is one contiguous buffer containing a record count, record stride, and `(slot_index, complete_slot_data)` records. One compute invocation writes one destination slot. Unique indices eliminate overlapping writes and destination atomics. Pool growth copies the old GPU buffer before applying patches; unchanged records are not re-uploaded from the CPU.

Scatter uploads reduce many small upload calls at the cost of an extra dispatch and an index per record. Their performance advantage is workload-dependent and has not been established by a benchmark. Dense updates may favor contiguous range writes. The implementation currently allocates staging buffers per nonempty batch and a readback buffer per capture; reuse is a possible optimization.

## Refinement demand and residency

Interactive rays atomically mark deferred leaves when the screen-space LOD test requires finer geometry. Multiple rays requesting the same leaf produce one flag. There is no CPU distance cutoff: distance alone must not reject detail that the renderer still asks for.

The `splits_per_frame` setting in `config.toml` limits successful CPU splits per frame receiving feedback (default 1,024; range 1–4,096). Larger budgets load detail in fewer frames but can increase CPU frame time. This is a split budget, not an upload-record limit: a split creates one branch and eight leaves and may patch its parent.

Requests beyond the successful-split budget are not kept in a CPU queue; subsequent rays rediscover them. Frames rendered while a feedback batch is pending do not enqueue another batch. Pool-limit failures retain coarse previews. Stable views should eventually reach zero requests and uploads when the required allocations fit, but finite march budgets can prevent rays from reaching all relevant geometry.

No resident node is evicted because it is distant or unused. Memory use can therefore grow as the camera explores. An eventual eviction policy should be driven by a residency budget and protect active ancestors. Approximate least-recent-use could be evaluated, but would need access information from traversal: split-request recency alone cannot identify exact geometry that remains visible. Eviction must remain paused during accumulation.

## Diagnostics and validation

The overlay separates resident state, sampled every 250 ms, from activity measured every render frame:

- Resident state includes branch/leaf counts and capacities, free slots, exact empty/solid leaves, deferred leaves, live/reserved GPU pool bytes, and baked height-bound bytes.
- GPU-to-CPU activity counts unique flagged leaf slots in the batch received this frame. Its source frame is reported separately; no received batch is distinct from a received empty batch.
- CPU-to-GPU activity counts unique live branch and leaf records patched this frame. It excludes uniforms, unchanged records copied during growth, and unreachable freed slots.
- Request outcomes include successful splits, budget deferrals, pool-limit failures, stale identities, no-longer-needed refinement, and requests discarded while paused.

An idle view can retain many deferred leaves outside its required LOD. Zero requests is not proof of complete traversal when rays can exhaust their march budget. GPU pool byte counts exclude request/readback buffers, staging, framebuffers, and CPU metadata.

`Tree::validate` checks reachable allocations, unique references, parent links, regions, and valid leaf data. The C control also compares every allocated GPU slot against its CPU record. Integration tests cover terrain bounds, local compatibility, allocation reuse, delayed-feedback invalidation, topology, and imports. GPU smoke tests exercise growth and scatter writes, frozen accumulation, residency retention, per-frame accounting, distant-view refinement, and fixed-view convergence against exact reference terrain.

## Packed scenes and scope

`core::packed` provides an independent input/reference encoding: a node-count header followed by tagged words, with branches pointing to eight contiguous packed children and leaves storing occupancy 0 or 1. Import validates the reachable structure and constructs runtime pool records; it does not preserve unreachable storage. `Renderer::replace_packed_scene` replaces the procedural scene, disables its terrain streaming, and resets accumulation. All runtime merging remains in `Tree`.

The current design supports a height-field terrain source and exact binary occupancy. It does not implement terrain editing, worker-thread generation, eviction, richer material compatibility, or background refinement during accumulation. Path tracing converges to the frozen resident and LOD approximation, not necessarily the fully refined terrain. See [rendering architecture](rendering.md) for traversal, lighting, sampling, and accumulation details.
