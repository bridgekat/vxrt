# Sparse voxel tree design

The renderer uses a CPU-owned sparse voxel octree (SVO). GPU rays read its resident representation and mark leaves that need refinement. The CPU generates children from baked terrain bounds, merges compatible leaves, and uploads changed slots. GPU shaders never allocate nodes or modify topology.

Interactive rendering permits refinement. Accumulating path tracing freezes the resident tree and camera; missing detail remains a coarse solid preview in both modes. Interactive frames also run a bounded CPU residency sweep, unloading detail that is too distant for the radial LOD policy. Nearby off-screen detail stays resident.

## Module boundaries

| Module | Responsibility |
| --- | --- |
| `src/core/residency.rs` | Radial LOD policy and bounded rolling unload cursor |
| `src/core/terrain.rs` | Height function and immutable aligned min/max pyramid |
| `src/core/node.rs` | Spatial regions, occupancy classification, local split and merge rules |
| `src/core/pool.rs` | Stable slot indices, freelist allocation, doubling growth, feedback invalidation |
| `src/core/tree.rs` | Tree ownership, public queries, local mutations, maintained counters |
| `src/core/tree/storage.rs` | GPU record encoding, typed pool queries, dirty records and feedback snapshot lifetime |
| `src/core/tree/import.rs` | Packed reference-scene construction using the same local normalization rules |
| `src/core/tree/diagnostics.rs` | Explicit reachability, consistency, compactness and counter validation |
| `src/core/packed.rs` | Packed scene validation and independent exact reference generation |
| `src/renderer/streaming.rs` | Feedback readback, refinement scheduling and activity counters |
| `src/renderer/uploads.rs` | GPU pool ownership, growth, scatter uploads and readback verification |
| `src/renderer/requests.rs` | Per-ray buffers and hierarchical bounded request merging |
| `shaders/main.wgsl` | Read-only traversal and per-ray refinement requests |
| `shaders/requests.wgsl` | 64-way request-list merging without deduplication |
| `shaders/scatter.wgsl` | Applying complete slot patches |

`core` has no windowing or GPU dependencies. One CPU owner currently performs tree operations on the render thread. The separation permits future worker scheduling, but there are no concurrent pool mutations or allocator locks today.

## Terrain bounds and local operations

The world is a cube of side `2^world_levels`, with supported depths 1–12 (default 10). Startup bakes a height field clipped to the world and the exclusive `max_height` cutoff. A pyramid stores minimum and maximum heights for every aligned `2^n × 2^n` XZ square. Each level reduces four squares, giving approximately `4/3 × side²` min/max pairs across all levels. Runtime refinement reads these bounds rather than evaluating terrain again. At depth 12, the side is 4,096 voxels and pyramid element storage is 178,956,968 bytes (about 170.7 MiB), sixteen times the depth-10 footprint. Startup baking remains proportional to world area; resident geometry is still generated on demand.

For a cubic region with vertical interval `[y, y + size)` and height bounds `[min, max]`:

- `y >= max` proves the region empty.
- `y + size <= min` proves it solid.
- Otherwise it is mixed and needs finer detail.

A mixed region is represented by a deferred leaf with solid occupancy. Splitting classifies its eight child regions independently. Child index bits 0, 1, and 2 select the upper half of X, Y, and Z respectively.

Every node stores `NodeData`: binary occupancy and a mixed-detail flag. Two invariants hold after every public mutation:

1. **Consistency:** each branch's data equals `node::merge_summarize` of its eight children's data. Eight exact empty children summarize to empty; eight exact solid children summarize to solid; all other combinations summarize to mixed, with solid occupancy as the coarse preview.
2. **Compactness:** if `node::try_merge_exact` accepts the eight child summaries, the node drops its children. Mixed summaries are never exact-compatible, even if all eight have preview occupancy 1.

Summary propagation and exact merge checks proceed bottom-up through parent links, examining eight direct children per level. They do not scan unrelated subtrees. Future material fields must participate in both summarization and exact compatibility.

## Runtime representation and allocation

Every logical node has a persistent data-pool slot, whether or not it currently has children. A separate branch pool stores child blocks. Richer materials can extend the data record without changing child descriptors.

| GPU record | Size | Contents |
| --- | --- | --- |
| Branch block | 36 bytes | Eight child descriptors, then the owner's data-slot index |
| Data | 16 bytes | Occupancy, mixed flag, child-block index plus one (zero for a leaf), reserved word |
| Ray request | 4 bytes per pixel or coarse beam | Zero or requested data-slot index plus one |
| Merged list | One count word plus bounded entries | Total raw count and retained request indices |

Descriptors store an index in bits 4–31 and a tag in bits 0–1: branch `1` references a child block, leaf `3` references a data slot. Both cache occupancy in bit 2 and mixed status in bit 3. Ordinary leaf traversal uses the cached flags without a dependent data-buffer read. At a branch LOD stop, traversal follows the child block's owner index and reads that node's data. This is the same summary retained by derefinement. Changes to a node's data or child-block link also dirty its parent's cached descriptor; the root descriptor comes from the scene uniform.

GPU pools have no packed-tree header. CPU entries additionally retain regions and parent links. The runtime encoding is distinct from the packed import format.

Each pool begins with 16 slots and doubles within adapter limits when allocation exhausts the freelist. Existing live slots never move. Freeing slots permits reuse without global relocation or compaction. Reserved capacity remains allocated after merging.

A `Node` handle identifies a data-slot allocation. Refinement and derefinement retain that handle. Removing the node as a descendant frees its allocation, after which its index may identify another node. There are no generation counters; delayed feedback uses the snapshot invariant below.

## Topology changes

After construction, `Tree` exposes only `refine` and `derefine` as public mutations. Read-only getters include `root_node`, `region`, `node_data`, `children`, and diagnostic enumeration. `stats()` returns named counters; storage diagnostics use `PoolKind::Branch` or `PoolKind::Data`, rather than numeric pool selectors. Invalid or ineligible mutations return false. Tree submodules share private implementation details without expanding the public mutation API. The same local normalization helper maintains summaries and exact merges during import and refinement. Dirty-set draining and feedback management are crate-private renderer integration methods; external callers cannot change child links or data independently.

Refinement checks capacity for one child block and eight data slots, then classifies and allocates the children. It attaches the block to the existing node, updates dirty records, and propagates summaries and exact merges through ancestors. Insufficient capacity leaves the original coarse node intact.

Exact merging retains the node's summary and data slot, detaches its child block, and releases descendants. Packed imports normalize bottom-up during construction. There is no periodic/global compaction pass or manual compaction command.

Derefinement simply drops the selected node's children and frees their allocations. Its already-consistent summary and data-slot identity remain unchanged. No terrain lookup or summary reconstruction is needed, and ancestor summaries and compatibility remain unchanged. A leaf or unavailable data slot is a no-op. Descendant reclamation still costs time proportional to the removed subtree; automatic unloading therefore only selects branches with eight leaf children. Freed GPU slots need not be cleared because the new topology cannot reach them.

Imported scenes support derefinement, but cannot regenerate discarded detail because the import source is not retained. Only refinement requires external generation data. Automatic compaction uses exact compatibility; lossy summarization alone must never trigger child removal.

Private helpers maintain exact-empty, exact-solid, and mixed **terminal leaf** counters, including transitions between a leaf and a branch. Data-pool usage counts all nodes; branch-pool usage counts child blocks; their difference is the number of leaves. Height-map bytes are cached at construction. Consequently, `stats()` reads maintained counters and pool metadata in constant time without periodic scans.

## GPU-visible state and delayed feedback

Two independent kinds of CPU bookkeeping are required:

| Bookkeeping | Meaning | Lifetime |
| --- | --- | --- |
| Dirty slot sets | Final live records not yet included in an upload batch | Drained by upload synchronization |
| Feedback invalidation sets | Slot identities changed since the GPU snapshot producing the outstanding readback | Cleared only after that batch is consumed or discarded |

At most one readback batch is outstanding. After uploads, traversal, and request merging have been recorded, capturing its packet starts an empty invalidation set for the data pool. The tracked snapshot is the state those ordered GPU commands will observe; it need not have finished executing when capture is recorded.

While that batch is outstanding, freeing or allocating a data slot inserts its index into the data-pool invalidation set. Pool growth preserves existing identities. A returned data-slot index is accepted only if it is still allocated and absent from the invalidation set. Its current refinement eligibility is then checked again. In-place content edits do not by themselves change identity.

The invariant is: **every accepted request still refers to the allocation that occupied its index in the GPU state that generated that request.** Logs remain active while processing the batch, because local merging or derefinement can release descendants whose slots are later reused. A node retained across refinement/derefinement keeps its identity; current eligibility still determines whether to split it.

Upload synchronization must not clear these logs. A newer upload may complete before feedback from an older snapshot is consumed. Command ordering protects GPU memory access, but cannot identify a reused CPU slot in an old readback. The invalidation sets supply that identity check without generation fields. Supporting multiple outstanding snapshots would require separate snapshot tracking or another identity scheme.

Scene replacement discards outstanding feedback. During accumulation, completed batches are counted and discarded without topology changes. New refinement requests are disabled while path tracing.

## Frame ordering and scattered uploads

The renderer uses this order:

1. Poll for a completed feedback batch without blocking. If interactive, process requests up to `splits_per_frame` successful splits with local merge propagation, then run the bounded CPU unload sweep.
2. Record GPU pool growth copies, then scatter patches for dirty branch and data slots. Rebuild bindings when buffers change.
3. Provide the current root and frame parameters. Enable request collection only if interactive and no readback is outstanding.
4. Run traversal: interactive beam pass followed by full-resolution rays, or the path-tracing pass. Collecting invocations initialize and write their own request slots.
5. If collecting, merge per-ray lists into one bounded packet, then copy that packet into the reusable readback buffer. Submit commands and request asynchronous mapping.

Copies and separate compute passes are ordered through wgpu command submission. Traversal sees complete topology updates, each merge level reads the previous level's finished output, and readback copies the final packet. CPU code reads a feedback buffer only after its mapping callback succeeds. No shader workgroup barrier is used as a substitute for ordering between dispatches.

The normal path uses `map_async`, nonblocking `PollType::Poll`, and `try_recv`; rendering continues when feedback is not ready. The copy still has GPU dependencies and consumes bandwidth, so asynchronous mapping does not imply free or fully overlapped transfers. C diagnostics and PNG export deliberately use synchronous readback.

Each nonempty pool update is one contiguous buffer containing a record count, record stride, and `(slot_index, complete_slot_data)` records. One compute invocation writes one destination slot. Unique indices eliminate overlapping writes and destination atomics. Pool growth copies the old GPU buffer before applying patches; unchanged records are not re-uploaded from the CPU.

Scatter uploads reduce many small upload calls at the cost of an extra dispatch and an index per record. Their performance advantage is workload-dependent and has not been established by a benchmark. Dense updates may favor contiguous range writes. Staging buffers are allocated per nonempty upload batch. The feedback readback buffer is reused after unmapping, and replaced when request capacity changes.

## Sparse request collection

The [ray-casting protocol](raycast.md#refinement-output-contract) produces at most one request per primary pixel and coarse beam. Each ray owns its output slot, so collection needs neither global atomics nor warp coordination. Duplicates remain on the GPU. Slot count is `width * height + (ceil(width/4) + 1) * (ceil(height/4) + 1)`, independent of tree size.

The `max_split_requests` setting bounds the final packet's entries (default 16,384; range 1–1,048,576). It counts raw entries, including duplicates, and is independent of the successful-split budget. A 64-way merge combines lists in each compute workgroup, using a shared-memory prefix scan to place nonempty entries. Further dispatches merge the resulting lists until one remains. Each record contains a total raw count followed by at most its capacity of retained entries. Intermediate capacity grows by 64 per level until reaching the configured bound. Counts include dropped entries, so overflow remains observable even when it occurs at intermediate levels.

The first level rotates the ray-slot order by the configured packet capacity between captures. Truncation retains a prefix of that rotated nonempty stream. This avoids permanently favoring one screen region; it is not a priority system or a guarantee of short loading latency. Duplicates can occupy most of a small packet. Unretained requests are rediscovered by later rays, with no persistent suppression bit preventing retries.

One fixed-size bounded packet is read back per captured batch, including when the count is zero. Its size is at most `4 * (max_split_requests + 1)` bytes (65,540 bytes at the default); very small viewports use a smaller final capacity. There is no count-first round trip or indirect copy. Only initialized entries within the count/capacity bound are decoded; unused tail words are ignored. CPU code sorts and deduplicates retained indices before checking snapshot validity and applying splits.

Collection and GPU temporary memory scale with ray count, and retained readback/CPU decoding scale with packet capacity rather than tree capacity. Merge work is still present in steady state, and the packet does not shrink to its actual count. For very large viewports, profile this bandwidth and dispatch cost; GPU deduplication could improve loading efficiency but is not needed for correctness. All request resources are rebuilt on resolution changes, while an already captured staging buffer and its invalidation logs remain valid until consumed.

## Refinement demand and residency

Refinement follows the screen-space LOD test without a CPU distance cutoff: distance alone must not reject detail that the renderer still asks for.

The `splits_per_frame` setting in `config.toml` limits successful CPU splits per frame receiving feedback (default 1,024; range 1–4,096). Larger budgets load detail in fewer frames but can increase CPU frame time. This is a split budget, not an upload-record limit: a split creates one child block and eight data records, updates its retained node, and may patch its parent.

Requests beyond the successful-split budget are not kept in a CPU queue; subsequent rays rediscover them. Frames rendered while a feedback batch is pending do not enqueue another batch. Pool-limit failures retain coarse previews. Stable views should eventually reach zero requests and uploads when the required allocations fit, but finite march budgets can prevent rays from reaching all relevant geometry.

### Bounded CPU unloading

`max_unload_checks` (default 16384, range 0–65536; zero disables unloading) limits branch-pool slots examined by a persistent rolling cursor each interactive frame. `unloads_per_frame` (default 8192, range 0–65536) separately caps logical nodes removed. Each successful subtree unload consumes eight units: the eight child data nodes, excluding the retained parent and its separate child-block allocation. The sweep stops when the check limit is reached or fewer than eight unload units remain, without exceeding either limit. Either zero budget disables unloading; values 1–7 for the unload budget cannot fit an operation. Unused budget does not carry over to the next frame. Holes count against the check budget; a frame visits no more than one pool capacity. No full-tree candidate collection or scan occurs. Pool growth and slot reuse need no cursor repair: each visit reads the current allocation. Scene replacement resets the cursor.

The radial policy is specified in [ray casting](raycast.md#shared-lod-and-residency-policy). CPU retention uses 1.5 times the rendering quality, creating a hysteresis band. It ignores orientation, visibility and frustum membership. A branch within this retention range is protected even behind the camera. A distant branch is eligible only when all eight children are leaves: one successful check releases exactly eight data slots and one child block. Parents become eligible on subsequent visits, so large subtrees disappear incrementally without unbounded recursive reclamation in a frame. Each check has at most eight child queries; allocator/dirty-set bookkeeping retains its usual costs.

This is distance-based residency, not LRU or a strict memory budget. Traversal emits no access feedback, and there are no GPU access bits or timestamps. Existing snapshot invalidation protects pending split feedback when descendants are freed or reused. Accumulation skips the sweep entirely; imported scenes are exempt because they cannot regenerate discarded detail. Pool capacities remain allocated for reuse, so unloading reduces live usage rather than returning VRAM. Sparse, previously large pools can take many frames to sweep.

## Diagnostics and validation

The overlay reads resident statistics and activity counters every render frame. All displayed tree statistics use maintained counters or cached metadata; there is no timed statistics scan.

- Resident state includes branch/data pool counts and capacities, terminal leaf counts, free slots, exact empty/solid leaves, deferred leaves, live/reserved GPU pool bytes, and baked height-bound bytes.
- GPU-to-CPU activity counts unique requested data-slot indices after CPU deduplication of the received packet. Its source frame is reported separately; no received batch is distinct from a received empty batch.
- Packet counters report retained raw entries (`request_entries`) and raw entries dropped by the bound (`request_dropped`). Duplicates count in these values; neither is a count of unique omitted nodes.
- CPU-to-GPU activity counts unique logical nodes updated this frame, taking the union of patched data indices and patched child-block owners. Physical branch/data patch counts are also available in `StreamingStats`. It excludes uniforms, unchanged records copied during growth, and unreachable freed slots.
- Request outcomes include successful splits, budget deferrals, pool-limit failures, stale identities, no-longer-needed refinement, and requests discarded while paused.

An idle view can retain many deferred leaves outside its required LOD. Zero requests is not proof of complete traversal when rays can exhaust their march budget. GPU pool byte counts exclude request/readback buffers, staging, framebuffers, and CPU metadata.

`Tree::validate` checks reachable allocations, unique references, parent links, regions, valid node data, branch ownership, consistency of every branch summary, and compactness: no branch may retain eight exact-compatible child summaries. It independently recounts leaf types and compares them with the maintained counters. The C control runs this explicit full-tree diagnostic without modifying the tree, then compares every allocated GPU slot against its CPU record. Public-API tests exercise repeated load/unload cycles, pool-limit failures, invalid handles, imported scenes, and mixed update sequences against independently counted leaf records. A GPU LOD regression compares resident branch stops against the same nodes after derefinement in interactive and accumulation modes. Crate-private synchronization regressions live in `tests/support/tree_feedback.rs`, including stale feedback after unloading and slot reuse. GPU smoke tests exercise growth and scatter writes, frozen accumulation, residency retention, per-frame accounting, distant-view refinement, and fixed-view convergence against exact reference terrain. Request tests compare hierarchical output against a CPU stream filter, including empty input, duplicates, rotation, truncation, partial groups, and 2D dispatch. A small-packet rendering test exercises overflow and CPU deduplication.

## Packed scenes and scope

Packed imports are used by regression tests for independent exact terrain, empty scenes, and enclosed/open-room lighting; the interactive application does not load packed scenes. `core::packed` provides their input/reference encoding: a node-count header followed by tagged words, with branches pointing to eight contiguous packed children and leaves storing occupancy 0 or 1. Import validates the reachable structure and constructs runtime pool records; it does not preserve unreachable storage. `Renderer::replace_packed_scene` replaces the procedural scene, disables its terrain streaming, and resets accumulation. All runtime merging remains in `Tree`.

The current design supports a height-field terrain source and exact binary occupancy. It does not implement terrain editing, worker-thread generation, LRU or memory-budget eviction, richer material compatibility, or background refinement during accumulation. Path tracing converges to the frozen resident and LOD approximation, not necessarily the fully refined terrain. See [ray casting](raycast.md) for traversal and profiling, and [sampling](sampling.md) for lighting and accumulation.

Useful optimization experiments include worker generation if CPU profiling supports it, coalesced range uploads versus scatter writes at different dirty densities, and upload-staging reuse. Measure active loading separately from steady state. Pool locality after allocation/reuse may affect traversal, but relocation would require reference updates and preservation of the feedback snapshot invariant.
