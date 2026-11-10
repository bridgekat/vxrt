// Merge 64 bounded lists per workgroup. No deduplication or global atomics.
struct Params {
    source_count: u32,
    source_capacity: u32,
    capacity: u32,
    rotation: u32
}

@group(0) @binding(0)
var<uniform> p: Params;
@group(0) @binding(1)
var<storage, read> source: array<u32>;
@group(0) @binding(2)
var<storage, read_write> destination: array<u32>;
var<workgroup> offsets: array<u32, 64>;
var<workgroup> totals: array<u32, 64>;

@compute @workgroup_size(64)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(local_invocation_index) lane: u32) {
    let group_index = group.x + group.y * 1024u;
    let first = group_index * 64u;
    if first >= p.source_count {
        return;
    }
    let index = first + lane;
    var count = 0u;
    if index < p.source_count {
        if p.source_capacity == 0u {
            count = select(0u, 1u, source[(index + p.rotation) % p.source_count] != 0u);
        }
        else {
            count = source[index * (p.source_capacity + 1u)];
        }
    }
    totals[lane] = count;
    offsets[lane] = select(min(count, p.source_capacity), count, p.source_capacity == 0u);
    workgroupBarrier();
    // Inclusive scan: stored counts determine placement, totals include dropped entries.
    for (var step = 1u; step < 64u; step *= 2u) {
        var a = 0u;
        var b = 0u;
        if lane >= step {
            a = offsets[lane - step];
            b = totals[lane - step];
        }
        workgroupBarrier();
        offsets[lane] += a;
        totals[lane] += b;
        workgroupBarrier();
    }
    let base = group_index * (p.capacity + 1u);
    if lane == 0u {
        destination[base] = totals[63u];
    }
    let retained = min(offsets[63u], p.capacity);
    for (var output = lane; output < retained; output += 64u) {
        // Find the child list containing this output element.
        var low = 0u;
        var high = 64u;
        while low < high {
            let mid = (low + high) / 2u;
            if offsets[mid] <= output {
                low = mid + 1u;
            }
            else {
                high = mid;
            }
        }
        let child = first + low;
        var value: u32;
        if p.source_capacity == 0u {
            value = source[(child + p.rotation) % p.source_count];
        }
        else {
            var preceding = 0u;
            if low > 0u {
                preceding = offsets[low - 1u];
            }
            value = source[child * (p.source_capacity + 1u) + 1u + output - preceding];
        }
        destination[base + 1u + output] = value;
    }
}
