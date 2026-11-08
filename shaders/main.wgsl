// Sparse-octree traversal, interactive shading, and Lambertian path transport.
struct Params  {
    projection: mat4x4<f32>, model_view: mat4x4<f32>,
    camera: vec4<f32>,
    // xyz position, w field of view in radians
    frame: vec4<u32>,
    // width, height, path tracing, profiler
    beam: vec4<u32>,
    // previous index/size, current index/size
    world: vec4<u32>,
    // dynamic, max nodes, max levels, noise levels
    misc: vec4<f32>,
    // partial levels, LOD quality, sky intensity, sun intensity
    sampling: vec4<u32>,
    // sample index, seed, accumulated count, coherent bounce samples
    traversal: vec4<u32>, // march budget, reserved
}
struct Tree  {
    count: atomic<u32>, data: array<atomic<u32>>
}
@group(0) @binding(0) var<uniform> p: Params;
@group(0) @binding(1) var<storage, read_write> tree: Tree;
@group(0) @binding(2) var<storage, read_write> output_count: u32;
@group(0) @binding(3) var<storage, read_write> frame: array<vec4<f32>>;
@group(0) @binding(4) var noise_tex: texture_2d<f32>;
@group(0) @binding(5) var max_tex: texture_2d<f32>;
@group(0) @binding(6) var min_tex: texture_2d<f32>;
@group(0) @binding(7) var<storage, read_write> beams: array<f32>;
const GAMMA=2.2;
struct Intersection  {
    pos:vec3<f32>, offset:vec3<f32>
}
struct Node  {
    data:u32, level:u32, box:vec4<f32>
}
var<private> root_size:f32;
var<private> noise_size:f32;
var<private> lod_center:vec3<f32>;
var<private> lod_dir:vec3<f32>;
var<private> beam_mode:bool;
var<private> stack:array<vec4<f32>,20>;
var<private> stp:u32;
// Per-invocation traversal state.
var<private> test_point:vec3<f32>;
var<private> last:Intersection;
fn sun_dir()->vec3<f32>  {
    return normalize(vec3<f32>(0.8,-1.0,0.3));
}
fn divide(v:vec4<f32>)->vec3<f32>  {
    return v.xyz/v.w;
}
fn inside(a:vec3<f32>,box:vec4<f32>)->bool  {
    return all(a>=box.xyz)&&all(a<box.xyz+box.w);
}
fn sky(d:vec3<f32>)->vec3<f32>  {
    let dir=normalize(d);
    if dot(-sun_dir(),dir)>=cos(SUN_ANGLE/2.0)  {
        return p.misc.w * pow(vec3<f32>(1.0,0.7,0.5)*1.5,vec3<f32>(GAMMA));
    }
    return p.misc.z * mix(pow(vec3<f32>(152.0,211.0,250.0)/255.0,vec3<f32>(GAMMA)),pow(vec3<f32>(90.0,134.0,206.0)/255.0,vec3<f32>(GAMMA)),smoothstep(0.0,1.0,dir.y*2.0));
}
fn palette(normal:vec3<f32>)->vec3<f32>  {
    let side=pow(vec3<f32>(147.5,166.4,77.0)/255.0,vec3<f32>(GAMMA));
    let vertical=pow(select(vec3<f32>(144.0,105.0,64.0),vec3<f32>(151.0,228.0,90.0),normal.y>=0.0)/255.0,vec3<f32>(GAMMA));
    return mat3x3<f32>(side,vertical,side)*(normal*normal);
}
var<private> pixel_index: u32;
fn random_dimension(dimension: u32) -> f32  {
    return path_sample_1d(pixel_index, p.sampling.x, dimension, p.sampling.y, p.sampling.w != 0u);
}
fn random_pair(dimension: u32) -> vec2<f32>  {
    return vec2<f32>(random_dimension(dimension), random_dimension(dimension + 1u));
}
fn allocate(count:u32)->u32  {
    // Free slots are cleared before dispatch; publishing a parent needs only one atomic store.
    loop  {
        let old=atomicLoad(&tree.count);
        if old>p.world.y-count  {
            return 0u;
        }
        let result=atomicCompareExchangeWeak(&tree.count,old,old+count);
        if result.exchanged  {
            return old;
        }
    }
    return 0u;
}
fn noise_subpixel(level:u32,x:vec2<u32>,maximum:bool)->f32  {
    let size=noise_size/f32(1u<<level);
    let pos=vec2<f32>(x)*size;
    let coord=vec2<i32>(pos);
    let mask=i32(noise_size)-1;
    let tex=vec4<f32>(textureLoad(noise_tex,(coord+vec2<i32>(0,0))&vec2<i32>(mask),0).r,textureLoad(noise_tex,(coord+vec2<i32>(1,0))&vec2<i32>(mask),0).r,textureLoad(noise_tex,(coord+vec2<i32>(0,1))&vec2<i32>(mask),0).r,textureLoad(noise_tex,(coord+vec2<i32>(1,1))&vec2<i32>(mask),0).r);
    let f=fract(pos);
    let fx=vec4<f32>(f.x,f.x+size,f.x,f.x+size);
    let fy=vec4<f32>(f.y,f.y,f.y+size,f.y+size);
    let res=mat4x4<f32>((1.0-fx)*(1.0-fy),fx*(1.0-fy),(1.0-fx)*fy,fx*fy)*tex;
    return select(min(min(res.x,res.y),min(res.z,res.w)),max(max(res.x,res.y),max(res.z,res.w)),maximum);
}
fn noise2d(level:u32,x:vec2<u32>,maximum:bool)->f32  {
    if level>p.world.w  {
        return noise_subpixel(level,x,maximum);
    }
    let mip=i32(p.world.w-level);
    if maximum  {
        return textureLoad(max_tex,vec2<i32>(x),mip).r;
    }
    return textureLoad(min_tex,vec2<i32>(x),mip).r;
}
fn height(level_in:u32,pos_in:vec2<u32>,maximum:bool)->f32  {
    var level=level_in+u32(p.misc.x);
    var pos=pos_in;
    var amplitude=pow(2.0,p.misc.x);
    var res=0.0;
    for(var i=0u;i+p.world.w<=p.world.z+u32(p.misc.x);i++)  {
        res+=noise2d(level,pos,maximum)*amplitude;
        amplitude*=0.5;
        if level>0u  {
            level--;
            pos-=pos&vec2<u32>(1u<<level);
        }
    }
    return res*root_size/256.0;
}
fn lod_check(level:u32,pos:vec3<u32>)->bool  {
    let rpos=(vec3<f32>(pos)+0.5)*root_size/f32(1u<<level)-lod_center;
    let size=root_size/f32(1u<<level);
    let real=tan(p.camera.w/2.0)*dot(rpos,lod_dir)*2.0/f32(p.frame.y);
    return size>real/p.misc.y && (!beam_mode || size>real*f32(p.beam.w));
}
fn generate_node(level:u32,pos:vec3<u32>)->u32  {
    let high=u32(height(level,pos.xz,true));
    if high<=(pos.y<<(p.world.z-level))  {
        return 3u;
    }
    let low=u32(height(level,pos.xz,false));
    if low>=((pos.y+1u)<<(p.world.z-level)) || level>=p.world.z  {
        return 7u;
    }
    let ptr=allocate(8u);
    return (ptr<<2u)+1u;
}
fn get_node(ptr:u32,level:u32,pos:vec3<u32>)->u32  {
    var data=atomicLoad(&tree.data[ptr]);
    if p.frame.z!=0u  {
        return select(data, 1u, data==0u);
    }
    if p.world.x!=0u && data==0u  {
        let result=atomicCompareExchangeWeak(&tree.data[ptr],0u,1u);
        if result.exchanged  {
            data=generate_node(level,pos);
            atomicStore(&tree.data[ptr],data);
        }
        else  {
            data=select(result.old_value,1u,result.old_value==0u);
        }
    }
    return data;
}
fn inner_intersect(org:vec3<f32>,dir:vec3<f32>,box:vec4<f32>)->Intersection  {
    let t=max((box.xyz-org)/dir,(box.xyz-org+box.w)/dir);
    let far=min(t.x,min(t.y,t.z));
    return Intersection(org+dir*far,select(vec3<f32>(0.0),sign(dir),t==vec3<f32>(far)));
}
fn outer_intersect(org:vec3<f32>,dir:vec3<f32>,box:vec4<f32>)->Intersection  {
    let a=min((box.xyz-org)/dir,(box.xyz-org+box.w)/dir);
    let b=max((box.xyz-org)/dir,(box.xyz-org+box.w)/dir);
    let near=max(a.x,max(a.y,a.z));
    let far=min(b.x,min(b.y,b.z));
    if far<near || near<0.0  {
        return Intersection(org,vec3<f32>(0.0));
    }
    return Intersection(org+dir*near,select(vec3<f32>(0.0),sign(dir),a==vec3<f32>(near)));
}
fn node_at(pos:vec3<u32>)->Node  {
    var box=vec4<f32>(0.0,0.0,0.0,root_size);
    var ptr=0u;
    for(var level=0u;level<=p.world.z;level++)  {
        let data=get_node(ptr,level,pos>>vec3<u32>(p.world.z-level));
        if data==1u || (data&3u)==3u  {
            return Node(data,level,box);
        }
        if !lod_check(level,pos>>vec3<u32>(p.world.z-level))  {
            return Node(1u,level,box);
        }
        ptr=data>>2u;
        let mid=box.xyz+box.w/2.0;
        if f32(pos.x)>=mid.x  {
            ptr+=1u;
            box.x=mid.x;
        }
        if f32(pos.y)>=mid.y  {
            ptr+=2u;
            box.y=mid.y;
        }
        if f32(pos.z)>=mid.z  {
            ptr+=4u;
            box.z=mid.z;
        }
        box.w/=2.0;
    }
    return Node(1u,p.world.z,box);
}
fn cast_ray(reference:vec3<f32>,direction:vec3<f32>)->f32  {
    let dir=normalize(direction);
    var box=vec4<f32>(0.0,0.0,0.0,root_size);
    if !inside(last.pos,box)  {
        last=outer_intersect(last.pos,dir,box);
        if all(last.offset==vec3<f32>(0.0))  {
            return -1.0;
        }
        test_point=clamp(last.pos,box.xyz+0.5,box.xyz+box.w-0.5);
    }
    var data:u32;
    if stp==0u  {
        data=get_node(0u,stp,vec3<u32>(0u));
    }
    else  {
        stp--;
        box=vec4<f32>(stack[stp].xyz,root_size/f32(1u<<stp));
        data=bitcast<u32>(stack[stp].w);
    }
    for(var i=0u;i<p.traversal.x;i++)  {
        while !inside(test_point,box)  {
            if stp==0u  {
                return -1.0;
            }
            stp--;
            box=vec4<f32>(stack[stp].xyz,root_size/f32(1u<<stp));
            data=bitcast<u32>(stack[stp].w);
        }
        let pos=vec3<u32>(test_point);
        while data!=1u && (data&3u)!=3u  {
            if stp>=20u  {
                return 1.0;
            }
            stack[stp]=vec4<f32>(box.xyz,bitcast<f32>(data));
            stp++;
            var ptr=data>>2u;
            let mid=box.xyz+box.w/2.0;
            if test_point.x>=mid.x  {
                ptr+=1u;
                box.x=mid.x;
            }
            if test_point.y>=mid.y  {
                ptr+=2u;
                box.y=mid.y;
            }
            if test_point.z>=mid.z  {
                ptr+=4u;
                box.z=mid.z;
            }
            box.w/=2.0;
            data=get_node(ptr,stp,pos>>vec3<u32>(p.world.z-stp));
            if (data&3u)!=3u && !lod_check(stp,pos>>vec3<u32>(p.world.z-stp))  {
                data=1u;
            }
        }
        if data==1u || (data>>2u)!=0u  {
            return f32(i)/f32(p.traversal.x);
        }
        let hit=inner_intersect(reference,dir,box);
        test_point=clamp(hit.pos,box.xyz+0.5,box.xyz+box.w-0.5)+hit.offset;
        last=hit;
    }
    return 1.0;
}
fn normal()->vec3<f32>  {
    if p.world.x!=0u  {
        let node=node_at(vec3<u32>(test_point));
        let level=node.level;
        let base=vec2<u32>(test_point.xz)>>vec2<u32>(p.world.z-level);
        let h00=height(level,base,true);
        let h10=height(level,base+vec2<u32>(1,0),true);
        let h01=height(level,base+vec2<u32>(0,1),true);
        let scale=pow(2.0,f32(p.world.z-level));
        return normalize(cross(vec3<f32>(0.0,(h01-h00)/scale,1.0),vec3<f32>(1.0,(h10-h00)/scale,0.0)));
    }
    return -last.offset;
}
fn start_ray(org:vec3<f32>)  {
    test_point=org;
    last=Intersection(org,vec3<f32>(0.0));
}
// Restart at the exact surface boundary, with a test point in the adjacent empty cell.
// Shadow and continuation rays have independent traversal stacks.
fn surface_ray(hit: Intersection, outside: vec3<f32>)  {
    stp = 0u;
    test_point = outside;
    last = Intersection(hit.pos, -hit.offset);
}
fn trace_path(org: vec3<f32>, direction: vec3<f32>) -> vec3<f32>  {
    start_ray(org);
    var dir = direction;
    var throughput = vec3<f32>(1.0);
    var radiance = vec3<f32>(0.0);
    var previous_pdf = 0.0;
    var bounce = 0u;
    loop  {
        let distance = cast_ray(last.pos, dir);
        if distance < 0.0  {
            var weight = 1.0;
            if bounce > 0u  {
                weight = power_weight(previous_pdf, sun_pdf(-sun_dir(), dir));
            }
            return radiance + throughput * sky(dir) * weight;
        }
        // A traversal-budget failure is not a surface hit or visible environment.
        if distance >= 1.0  {
            return radiance;
        }
        let hit = last;
        let outside = test_point - hit.offset;
        if dot(hit.offset, hit.offset) == 0.0  {
            return radiance;
        }
        // Use the actual voxel face for transport; terrain-gradient normals are display-only.
        let n = normalize(-hit.offset);
        let albedo = palette(n);
        let dimension = 2u + bounce * 5u;
        // One light sample, uniform in solid angle over the solar disk.
        let light_dir = sun_cone(-sun_dir(), random_pair(dimension));
        let cosine = max(dot(n, light_dir), 0.0);
        let light_pdf = sun_pdf(-sun_dir(), light_dir);
        if cosine > 0.0 && light_pdf > 0.0  {
            surface_ray(hit, outside);
            if cast_ray(hit.pos, light_dir) < 0.0  {
                let bsdf_pdf = cosine / PI;
                radiance += throughput * albedo * sky(light_dir)
                * (cosine / (PI * light_pdf)) * power_weight(light_pdf, bsdf_pdf);
            }
        }
        // Lambertian f*cos/pdf simplifies to albedo for cosine-weighted sampling.
        dir = cosine_hemisphere(n, random_pair(dimension + 2u));
        previous_pdf = cosine_pdf(n, dir);
        throughput *= albedo;
        bounce++;
        // Unbiased stochastic termination; no untested sky contribution or fixed bounce cutoff.
        if bounce >= 3u  {
            let survive = clamp(max(throughput.x, max(throughput.y, throughput.z)), 0.05, 0.95);
            if random_dimension(dimension + 4u) >= survive  {
                return radiance;
            }
            throughput /= survive;
        }
        surface_ray(hit, outside);
    }
    return radiance;
}
fn test_ray(reference:vec3<f32>,org:vec3<f32>,dir:vec3<f32>)->vec3<f32>  {
    start_ray(org);
    let distance=cast_ray(reference,dir);
    if p.frame.w!=0u  {
        return vec3<f32>(max(distance,0.0));
    }
    if distance<0.0  {
        return sky(dir);
    }
    let n=normal();
    var res=palette(n);
    res=mix(res*0.2,res,clamp(dot(n,-sun_dir()),0.0,1.0));
    res=mix(sky(dir),res,clamp((1.0-distance)*2.0,0.0,1.0));
    let fs=vec3<u32>(abs(last.pos-round(last.pos))<=vec3<f32>(0.05));
    if fs.x+fs.y+fs.z>=2u  {
        res=mix(res*0.2,res,clamp(length(last.pos-reference)/256.0,0.0,1.0));
    }
    return res;
}
@compute @workgroup_size(8,8,1)
fn main(@builtin(global_invocation_id) id:vec3<u32>)  {
    root_size=f32(1u<<p.world.z);
    noise_size=f32(1u<<p.world.w);
    let pos=p.camera.xyz;
    let center_dir=normalize(divide(p.model_view*p.projection*vec4<f32>(0.0,0.0,1.0,1.0)));
    lod_center=floor(pos)+0.5;
    lod_dir=center_dir;
    beam_mode=p.beam.z<1u;
    let pixel=id.xy;
    pixel_index=pixel.y*p.frame.x+pixel.x;
    let beam_width=(p.frame.x+3u)/4u+1u;
    let extent=select(p.frame.xy,(p.frame.xy+vec2<u32>(3u))/4u+vec2<u32>(1u),beam_mode);
    if any(pixel>=extent)  {
        return;
    }
    var beam_result=0.0;
    if p.beam.x<1u && p.frame.z==0u  {
        let base=pixel/(p.beam.y/p.beam.w);
        let index=base.y*beam_width+base.x;
        beam_result=min(min(beams[index],beams[index+1u]),min(beams[index+beam_width],beams[index+beam_width+1u]))-1e-2;
    }
    let coords=vec2<f32>(pixel*p.beam.w)/vec2<f32>(p.frame.xy)*2.0-1.0;
    var dithered=coords;
    if !beam_mode && p.frame.z!=0u  {
        dithered+=2.0*random_pair(0u)/vec2<f32>(p.frame.xy);
    }
    let dir=normalize(divide(p.model_view*p.projection*vec4<f32>(dithered,1.0,1.0)));
    if beam_mode  {
        start_ray(pos+dir*beam_result);
        let distance=cast_ray(pos,dir);
        var value=1e18;
        if distance>=0.0  {
            let real=tan(p.camera.w/2.0)*dot(last.pos-pos,lod_dir)*2.0/f32(p.frame.y);
            value=length(last.pos-pos)-real*f32(p.beam.w)/sqrt(2.0);
        }
        beams[pixel.y*beam_width+pixel.x]=value;
        return;
    }
    var color:vec3<f32>;
    if p.frame.z!=0u  {
        color=trace_path(pos+dir*beam_result,dir);
    }
    else  {
        color=test_ray(pos,pos+dir*beam_result,dir);
    }
    let index=pixel.y*p.frame.x+pixel.x;
    if p.frame.z!=0u && p.sampling.z>0u  {
        color=mix(frame[index].rgb,color,1.0/(f32(p.sampling.z)+1.0));
    }
    frame[index]=vec4<f32>(color,1.0);
    if all(pixel==vec2<u32>(0u))  {
        output_count=atomicLoad(&tree.count);
    }
}
