// Each slot appears at most once in a batch; one invocation owns its entire record.
struct Updates { count: u32, stride: u32, records: array<u32> }
@group(0) @binding(0) var<storage, read_write> destination: array<u32>;
@group(0) @binding(1) var<storage, read> updates: Updates;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let index = id.x + id.y * 65536u;
    if index < updates.count {
        let source = index * (updates.stride + 1u);
        let destination_offset = updates.records[source] * updates.stride;
        for (var word = 0u; word < updates.stride; word++) {
            destination[destination_offset + word] = updates.records[source + 1u + word];
        }
    }
}

