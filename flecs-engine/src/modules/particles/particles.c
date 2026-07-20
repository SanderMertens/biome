#define FLECS_ENGINE_PARTICLES_IMPL
#include "particles.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define FLECS_PARTICLES_DEFAULT_CAP 256

ECS_COMPONENT_DECLARE(FlecsParticleEnvelopeMode);
ECS_COMPONENT_DECLARE(FlecsParticleSortMode);
ECS_COMPONENT_DECLARE(FlecsParticleEnvelope);
ECS_COMPONENT_DECLARE(FlecsParticles);
ECS_COMPONENT_DECLARE(FlecsParticleEmitter);
ECS_COMPONENT_DECLARE(FlecsParticleBurst);
ECS_COMPONENT_DECLARE(FlecsParticleWind);
ECS_COMPONENT_DECLARE(FlecsParticleGround);
ECS_COMPONENT_DECLARE(FlecsParticlesImpl);

typedef struct {
    float pos[3];
    float size;
    float stretch;
    uint32_t color;
    float emissive;
    float axis[3];
} flecs_particle_instance_t;

typedef struct FlecsParticlesImpl {
    ecs_query_t *emitter_query;

    /* CPU gather scratch, rebuilt every frame */
    flecs_particle_instance_t *instances;
    float *depths;
    int32_t instance_cap;

    /* GPU */
    WGPURenderPipeline pipeline;
    WGPUTextureFormat pipeline_format;
    int32_t pipeline_samples;
    WGPUBindGroupLayout bind_layout;
    WGPUBindGroup bind_group;
    WGPUBuffer bound_uniforms;
    WGPUBuffer qbuf;
    WGPUBuffer ibuf;
    uint64_t ibuf_size;
} FlecsParticlesImpl;

static const char *kParticlesShaderSource =
    "struct Uniforms {\n"
    "  vp : mat4x4<f32>,\n"
    "  inv_vp : mat4x4<f32>,\n"
    "  light_vp : array<mat4x4<f32>, 4>,\n"
    "  cascade_splits : vec4<f32>,\n"
    "  light_ray_dir : vec4<f32>,\n"
    "  light_color : vec4<f32>,\n"
    "  camera_pos : vec4<f32>,\n"
    "  shadow_info : vec4<f32>,\n"
    "  ambient_light : vec4<f32>,\n"
    "  cloud_shadow_params : vec4<f32>\n"
    "}\n"
    "@group(0) @binding(0) var<uniform> uniforms : Uniforms;\n"
    "struct VertexIn {\n"
    "  @location(0) corner : vec2<f32>,\n"
    "  @location(1) center : vec3<f32>,\n"
    "  @location(2) size_stretch : vec2<f32>,\n"
    "  @location(3) color : vec4<f32>,\n"
    "  @location(4) emissive : f32,\n"
    "  @location(5) axis : vec3<f32>,\n"
    "};\n"
    "struct VertexOut {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) color : vec4<f32>,\n"
    "  @location(1) uv : vec2<f32>,\n"
    "};\n"
    "@vertex fn vs_main(in : VertexIn) -> VertexOut {\n"
    "  let to_cam = normalize(uniforms.camera_pos.xyz - in.center);\n"
    "  var up = vec3<f32>(0.0, 1.0, 0.0);\n"
    "  var right : vec3<f32>;\n"
    "  if (dot(in.axis, in.axis) > 1e-8) {\n"
    "    up = normalize(in.axis);\n"
    "    right = cross(up, to_cam);\n"
    "    let rl = length(right);\n"
    "    if (rl < 1e-4) {\n"
    "      right = vec3<f32>(1.0, 0.0, 0.0);\n"
    "    } else {\n"
    "      right = right / rl;\n"
    "    }\n"
    "  } else {\n"
    "    right = cross(vec3<f32>(0.0, 1.0, 0.0), to_cam);\n"
    "    let rl = length(right);\n"
    "    if (rl < 1e-4) {\n"
    "      right = vec3<f32>(1.0, 0.0, 0.0);\n"
    "    } else {\n"
    "      right = right / rl;\n"
    "    }\n"
    "    if (in.size_stretch.y <= 1.001) {\n"
    "      up = normalize(cross(to_cam, right));\n"
    "    }\n"
    "  }\n"
    "  let world = in.center\n"
    "    + right * (in.corner.x * in.size_stretch.x)\n"
    "    + up * (in.corner.y * in.size_stretch.x * in.size_stretch.y);\n"
    "  var out : VertexOut;\n"
    "  out.pos = uniforms.vp * vec4<f32>(world, 1.0);\n"
    "  let lin = in.color.rgb * in.color.rgb;\n"
    "  let light = uniforms.ambient_light.rgb\n"
    "    + uniforms.light_color.rgb * 0.75\n"
    "    + vec3<f32>(in.emissive);\n"
    "  out.color = vec4<f32>(lin * light, in.color.a);\n"
    "  out.uv = in.corner * 2.0;\n"
    "  return out;\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {\n"
    "  let d = dot(in.uv, in.uv);\n"
    "  let a = in.color.a * clamp(1.15 - d * 1.15, 0.0, 1.0);\n"
    "  return vec4<f32>(in.color.rgb * a, a);\n"
    "}\n";

static void flecsEngine_particles_releaseImpl(
    FlecsParticlesImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(impl->bind_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(impl->bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(impl->qbuf, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(impl->ibuf, wgpuBufferRelease);
    impl->ibuf_size = 0;
    impl->bound_uniforms = NULL;
    if (impl->emitter_query) {
        ecs_query_fini(impl->emitter_query);
        impl->emitter_query = NULL;
    }
    ecs_os_free(impl->instances);
    ecs_os_free(impl->depths);
    impl->instances = NULL;
    impl->depths = NULL;
    impl->instance_cap = 0;
}

ECS_DTOR(FlecsParticlesImpl, ptr, {
    flecsEngine_particles_releaseImpl(ptr);
})

ECS_MOVE(FlecsParticlesImpl, dst, src, {
    flecsEngine_particles_releaseImpl(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_DTOR(FlecsParticles, ptr, {
    ecs_os_free(ptr->particles);
    ptr->particles = NULL;
    ptr->count = 0;
})

ECS_MOVE(FlecsParticles, dst, src, {
    ecs_os_free(dst->particles);
    *dst = *src;
    src->particles = NULL;
    src->count = 0;
})

static float flecsEngine_particles_ease(
    FlecsParticleEnvelopeMode mode,
    float u)
{
    switch (mode) {
    case FlecsParticleEnvelopeEaseIn:
        return u * u;
    case FlecsParticleEnvelopeEaseOut:
        return u * (2.0f - u);
    case FlecsParticleEnvelopeEaseInOut:
        return u * u * (3.0f - 2.0f * u);
    default:
        return u;
    }
}

static float flecsEngine_particles_envelope(
    const FlecsParticleEnvelope *env,
    float t)
{
    if (env->mode == FlecsParticleEnvelopeNone) {
        return 1.0f;
    }
    float v = 1.0f;
    if (env->fade_in > 0 && t < env->fade_in) {
        v = flecsEngine_particles_ease(env->mode, t / env->fade_in);
    }
    if (env->fade_out > 0 && t > 1.0f - env->fade_out) {
        float o = flecsEngine_particles_ease(env->mode,
            (1.0f - t) / env->fade_out);
        if (o < v) {
            v = o;
        }
    }
    if (v < 0) {
        v = 0;
    }
    return v;
}

static int32_t flecsEngine_particles_poolCap(
    const FlecsParticles *p)
{
    return p->capacity > 0 ? p->capacity : FLECS_PARTICLES_DEFAULT_CAP;
}

void flecsEngine_particlesEmit(
    ecs_world_t *world,
    ecs_entity_t pool,
    const flecs_particle_t *particle)
{
    FlecsParticles *p = ecs_ensure(world, pool, FlecsParticles);
    int32_t cap = flecsEngine_particles_poolCap(p);
    if (!p->particles) {
        p->particles = ecs_os_calloc_n(flecs_particle_t, cap);
        p->count = 0;
    }

    if (p->count >= cap) {
        p->stat_dropped ++;
        return;
    }

    flecs_particle_t *slot = &p->particles[p->count ++];
    p->stat_emitted ++;

    *slot = *particle;
    if (slot->stretch <= 0) slot->stretch = 1.0f;
    if (slot->max_age <= 0) slot->max_age = 1.0f;
    if (slot->size <= 0) slot->size = 0.1f;
    if (slot->mass <= 0) slot->mass = 1.0f;
    if (!slot->color.r && !slot->color.g && !slot->color.b &&
        !slot->color.a)
    {
        slot->color = (FlecsRgba){ 255, 255, 255, 255 };
    }
}

static void FlecsParticlesUpdate(ecs_iter_t *it) {
    FlecsParticles *emitters = ecs_field(it, FlecsParticles, 0);
    float dt = it->delta_time;
    if (dt > 0.1f) dt = 0.1f;

    for (int32_t e = 0; e < it->count; e ++) {
        FlecsParticles *p = &emitters[e];
        float damp = 1.0f - p->drag * dt;
        if (damp < 0) damp = 0;

        for (int32_t i = 0; i < p->count; i ++) {
            flecs_particle_t *pt = &p->particles[i];
            pt->age += dt;
            if (pt->age >= pt->max_age) {
                p->particles[i] = p->particles[-- p->count];
                p->stat_expired ++;
                i --;
                continue;
            }
            pt->vel[1] -= p->gravity * dt;
            pt->vel[0] *= damp;
            pt->vel[1] *= damp;
            pt->vel[2] *= damp;
            pt->pos[0] += pt->vel[0] * dt;
            pt->pos[1] += pt->vel[1] * dt;
            pt->pos[2] += pt->vel[2] * dt;
            pt->size += pt->grow * dt;
            if (pt->size < 0.01f) pt->size = 0.01f;
        }
    }
}

static float flecsEngine_particles_randRange(
    float variance)
{
    if (variance <= 0) {
        return 0.0f;
    }
    float t = (float)rand() / (float)RAND_MAX;
    return (t * 2.0f - 1.0f) * variance;
}

static void flecsEngine_particles_spawn(
    ecs_world_t *world,
    ecs_entity_t pool,
    const float pos[3],
    float size,
    float size_variance,
    float grow,
    float stretch,
    float max_age,
    float max_age_variance,
    FlecsRgba color,
    flecs_vec3_t velocity,
    flecs_vec3_t velocity_variance,
    flecs_vec3_t spread,
    float radial,
    float mass)
{
    float ox = flecsEngine_particles_randRange(spread.x * 0.5f);
    float oy = flecsEngine_particles_randRange(spread.y * 0.5f);
    float oz = flecsEngine_particles_randRange(spread.z * 0.5f);

    flecs_particle_t particle = {0};
    particle.pos[0] = pos[0] + ox;
    particle.pos[1] = pos[1] + oy;
    particle.pos[2] = pos[2] + oz;
    particle.vel[0] = velocity.x + flecsEngine_particles_randRange(velocity_variance.x);
    particle.vel[1] = velocity.y + flecsEngine_particles_randRange(velocity_variance.y);
    particle.vel[2] = velocity.z + flecsEngine_particles_randRange(velocity_variance.z);

    if (radial != 0) {
        float len = sqrtf(ox * ox + oz * oz);
        if (len > 1e-4f) {
            particle.vel[0] += radial * ox / len;
            particle.vel[2] += radial * oz / len;
        }
    }
    particle.size = size + flecsEngine_particles_randRange(size_variance);
    particle.grow = grow;
    particle.stretch = stretch;
    particle.max_age = max_age + flecsEngine_particles_randRange(max_age_variance);
    particle.mass = mass;
    particle.color = color;
    flecsEngine_particlesEmit(world, pool, &particle);
}

static void flecsEngine_particles_worldPos(
    const FlecsWorldTransform3 *transform,
    flecs_vec3_t offset,
    float out[3])
{
    out[0] = offset.x;
    out[1] = offset.y;
    out[2] = offset.z;
    if (transform) {
        out[0] += transform->m[3][0];
        out[1] += transform->m[3][1];
        out[2] += transform->m[3][2];
    }
}

static void FlecsParticleEmitterUpdate(ecs_iter_t *it) {
    FlecsParticleEmitter *emitters = ecs_field(it, FlecsParticleEmitter, 0);
    const FlecsWorldTransform3 *transforms = ecs_field(it, FlecsWorldTransform3, 1);
    float dt = it->delta_time;
    if (dt > 0.1f) dt = 0.1f;

    for (int32_t i = 0; i < it->count; i ++) {
        FlecsParticleEmitter *em = &emitters[i];
        if (!em->enabled || em->rate <= 0) {
            continue;
        }

        em->accum += em->rate * dt;
        int32_t n = (int32_t)em->accum;
        if (n <= 0) {
            continue;
        }
        em->accum -= (float)n;

        ecs_entity_t pool = em->pool ? em->pool : it->entities[i];
        float pos[3];
        flecsEngine_particles_worldPos(
            transforms ? &transforms[i] : NULL, em->offset, pos);

        for (int32_t p = 0; p < n; p ++) {
            flecsEngine_particles_spawn(it->world, pool, pos,
                em->size, em->size_variance, em->grow, em->stretch,
                em->max_age, em->max_age_variance, em->color,
                em->velocity, em->velocity_variance, em->spread, em->radial,
                em->mass);
        }
    }
}

void flecsEngine_particlesBurst(
    ecs_world_t *world,
    ecs_entity_t pool,
    const float pos[3],
    const FlecsParticleBurst *burst)
{
    float p[3] = {
        pos[0] + burst->offset.x,
        pos[1] + burst->offset.y,
        pos[2] + burst->offset.z
    };

    for (int32_t i = 0; i < burst->count; i ++) {
        flecsEngine_particles_spawn(world, pool, p,
            burst->size, burst->size_variance, burst->grow, burst->stretch,
            burst->max_age, burst->max_age_variance, burst->color,
            burst->velocity, burst->velocity_variance, burst->spread,
            burst->radial, burst->mass);
    }
}

static void FlecsParticleBurstUpdate(ecs_iter_t *it) {
    FlecsParticleBurst *bursts = ecs_field(it, FlecsParticleBurst, 0);
    const FlecsWorldTransform3 *transforms = ecs_field(it, FlecsWorldTransform3, 1);

    for (int32_t i = 0; i < it->count; i ++) {
        FlecsParticleBurst *b = &bursts[i];
        ecs_entity_t e = it->entities[i];
        ecs_entity_t pool = b->pool ? b->pool : e;
        float pos[3];
        flecsEngine_particles_worldPos(
            transforms ? &transforms[i] : NULL, (flecs_vec3_t){0}, pos);

        flecsEngine_particlesBurst(it->world, pool, pos, b);

        ecs_remove(it->world, e, FlecsParticleBurst);
    }
}

static void FlecsParticleWindUpdate(ecs_iter_t *it) {
    FlecsParticleWind *winds = ecs_field(it, FlecsParticleWind, 0);
    float dt = it->delta_time;
    if (dt > 0.1f) dt = 0.1f;

    for (int32_t i = 0; i < it->count; i ++) {
        FlecsParticleWind *w = &winds[i];
        if (!w->field || w->width <= 0 || w->height <= 0 ||
            w->cell_size <= 0 || w->strength <= 0)
        {
            continue;
        }

        ecs_entity_t pool_ent = w->pool ? w->pool : it->entities[i];
        FlecsParticles *p = ecs_get_mut(it->world, pool_ent, FlecsParticles);
        if (!p) {
            continue;
        }

        int32_t half_w = w->width / 2;
        int32_t half_h = w->height / 2;
        for (int32_t j = 0; j < p->count; j ++) {
            flecs_particle_t *pt = &p->particles[j];
            int32_t xi = (int32_t)floorf(pt->pos[0] / w->cell_size) + half_w;
            int32_t zi = (int32_t)floorf(pt->pos[2] / w->cell_size) + half_h;
            if (xi < 0 || xi >= w->width || zi < 0 || zi >= w->height) {
                continue;
            }
            const flecs_vec2_t *v = &w->field[zi * w->width + xi];
            float m = pt->mass > 0 ? pt->mass : 1.0f;
            float k = w->strength / m * dt;
            if (k > 1.0f) k = 1.0f;
            pt->vel[0] += (v->x - pt->vel[0]) * k;
            pt->vel[2] += (v->y - pt->vel[2]) * k;
        }
    }
}

static void FlecsParticleGroundUpdate(ecs_iter_t *it) {
    const FlecsParticleGround *grounds = ecs_field(it, FlecsParticleGround, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        const FlecsParticleGround *g = &grounds[i];
        if (!g->corner_heights || g->width <= 0 || g->depth <= 0 ||
            g->cell_size <= 0)
        {
            continue;
        }

        ecs_entity_t pool_ent = g->pool ? g->pool : it->entities[i];
        FlecsParticles *p = ecs_get_mut(it->world, pool_ent, FlecsParticles);
        if (!p) {
            continue;
        }

        const float *h = g->corner_heights;
        int32_t stride = g->width + 1;
        for (int32_t j = 0; j < p->count; j ++) {
            flecs_particle_t *pt = &p->particles[j];
            float fx = (pt->pos[0] - g->origin.x) / g->cell_size;
            float fz = (pt->pos[2] - g->origin.y) / g->cell_size;
            int32_t xi = (int32_t)floorf(fx);
            int32_t zi = (int32_t)floorf(fz);
            if (xi < 0 || xi >= g->width || zi < 0 || zi >= g->depth) {
                continue;
            }
            float tx = fx - (float)xi;
            float tz = fz - (float)zi;
            const float *row0 = &h[zi * stride + xi];
            const float *row1 = &h[(zi + 1) * stride + xi];
            float h0 = row0[0] + (row0[1] - row0[0]) * tx;
            float h1 = row1[0] + (row1[1] - row1[0]) * tx;
            float ground_h = h0 + (h1 - h0) * tz;
            if (pt->pos[1] <= ground_h) {
                p->particles[j] = p->particles[-- p->count];
                p->stat_ground ++;
                j --;
            }
        }
    }
}

/* Back-to-front shell sort keyed on view depth. Particle counts are
 * small (hundreds), so this beats dragging in qsort_r portability. */
static void flecsEngine_particles_sort(
    flecs_particle_instance_t *instances,
    float *depths,
    int32_t count)
{
    for (int32_t gap = count / 2; gap > 0; gap /= 2) {
        for (int32_t i = gap; i < count; i ++) {
            flecs_particle_instance_t tmp = instances[i];
            float d = depths[i];
            int32_t j = i;
            while (j >= gap && depths[j - gap] < d) {
                instances[j] = instances[j - gap];
                depths[j] = depths[j - gap];
                j -= gap;
            }
            instances[j] = tmp;
            depths[j] = d;
        }
    }
}

int32_t flecsEngine_particles_prepare(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl)
{
    if (!ecs_id(FlecsParticlesImpl)) {
        return 0;
    }
    FlecsParticlesImpl *impl = ecs_singleton_ensure(
        world, FlecsParticlesImpl);

    if (!impl->emitter_query) {
        impl->emitter_query = ecs_query(world, {
            .terms = {{ .id = ecs_id(FlecsParticles), .inout = EcsIn }},
            .cache_kind = EcsQueryCacheAuto
        });
        if (!impl->emitter_query) {
            return 0;
        }
    }

    int32_t total = 0;
    int32_t sorted = 0;
    ecs_iter_t cit = ecs_query_iter(world, impl->emitter_query);
    while (ecs_query_next(&cit)) {
        const FlecsParticles *emitters = ecs_field(&cit, FlecsParticles, 0);
        for (int32_t e = 0; e < cit.count; e ++) {
            total += emitters[e].count;
            if (emitters[e].sort_mode != FlecsParticleSortNone) {
                sorted += emitters[e].count;
            }
        }
    }
    if (!total) {
        return 0;
    }

    if (impl->instance_cap < total) {
        int32_t cap = impl->instance_cap > 0 ? impl->instance_cap : 512;
        while (cap < total) cap *= 2;
        impl->instances = ecs_os_realloc_n(
            impl->instances, flecs_particle_instance_t, cap);
        impl->depths = ecs_os_realloc_n(impl->depths, float, cap);
        impl->instance_cap = cap;
    }

    const float *cam = view_impl->camera_pos;
    int32_t sorted_count = 0;
    int32_t unsorted_count = sorted;
    ecs_iter_t it = ecs_query_iter(world, impl->emitter_query);
    while (ecs_query_next(&it)) {
        const FlecsParticles *emitters = ecs_field(&it, FlecsParticles, 0);
        for (int32_t e = 0; e < it.count; e ++) {
            const FlecsParticles *p = &emitters[e];
            for (int32_t i = 0; i < p->count; i ++) {
                const flecs_particle_t *pt = &p->particles[i];
                int32_t index = p->sort_mode == FlecsParticleSortNone
                    ? unsorted_count ++
                    : sorted_count ++;
                flecs_particle_instance_t *inst = &impl->instances[index];
                float t = pt->max_age > 0 ? pt->age / pt->max_age : 1.0f;
                inst->pos[0] = pt->pos[0];
                inst->pos[1] = pt->pos[1];
                inst->pos[2] = pt->pos[2];
                inst->size = pt->size *
                    flecsEngine_particles_envelope(&p->size_envelope, t);
                inst->stretch = pt->stretch > 0 ? pt->stretch : 1.0f;
                inst->axis[0] = pt->axis[0];
                inst->axis[1] = pt->axis[1];
                inst->axis[2] = pt->axis[2];
                inst->emissive = p->emissive *
                    flecsEngine_particles_envelope(&p->emissive_envelope, t);

                float fade = flecsEngine_particles_envelope(
                    &p->alpha_envelope, t);
                uint32_t a = (uint32_t)((float)pt->color.a * fade);
                inst->color = (uint32_t)pt->color.r |
                    ((uint32_t)pt->color.g << 8) |
                    ((uint32_t)pt->color.b << 16) |
                    (a << 24);

                if (p->sort_mode != FlecsParticleSortNone) {
                    float dx = pt->pos[0] - cam[0];
                    float dy = pt->pos[1] - cam[1];
                    float dz = pt->pos[2] - cam[2];
                    impl->depths[index] = dx * dx + dy * dy + dz * dz;
                }
            }
        }
    }

    flecsEngine_particles_sort(
        impl->instances, impl->depths, sorted_count);

    uint64_t bytes = (uint64_t)total * sizeof(flecs_particle_instance_t);
    if (!impl->ibuf || impl->ibuf_size < bytes) {
        FLECS_WGPU_RELEASE(impl->ibuf, wgpuBufferRelease);
        uint64_t size = 16384;
        while (size < bytes) size *= 2;
        impl->ibuf = wgpuDeviceCreateBuffer(engine->device,
            &(WGPUBufferDescriptor){
                .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                .size = size
            });
        if (!impl->ibuf) {
            return 0;
        }
        impl->ibuf_size = size;
    }
    wgpuQueueWriteBuffer(engine->queue, impl->ibuf, 0, impl->instances,
        bytes);

    return total;
}

static bool flecsEngine_particles_ensurePipeline(
    FlecsParticlesImpl *impl,
    FlecsEngineImpl *engine,
    WGPUTextureFormat color_format,
    int32_t sample_count)
{
    if (impl->pipeline && impl->pipeline_format == color_format &&
        impl->pipeline_samples == sample_count)
    {
        return true;
    }

    FLECS_WGPU_RELEASE(impl->pipeline, wgpuRenderPipelineRelease);

    if (!impl->bind_layout) {
        WGPUBindGroupLayoutEntry entry = {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex,
            .buffer.type = WGPUBufferBindingType_Uniform
        };
        impl->bind_layout = wgpuDeviceCreateBindGroupLayout(
            engine->device, &(WGPUBindGroupLayoutDescriptor){
                .entries = &entry,
                .entryCount = 1
            });
        if (!impl->bind_layout) {
            return false;
        }
    }

    WGPUShaderModule module = flecsEngine_createShaderModule(
        engine->device, kParticlesShaderSource);
    if (!module) {
        return false;
    }

    WGPUVertexAttribute quad_attrs[1] = {
        { .format = WGPUVertexFormat_Float32x2, .offset = 0,
          .shaderLocation = 0 }
    };
    WGPUVertexAttribute inst_attrs[5] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0,
          .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Float32x2,
          .offset = offsetof(flecs_particle_instance_t, size),
          .shaderLocation = 2 },
        { .format = WGPUVertexFormat_Unorm8x4,
          .offset = offsetof(flecs_particle_instance_t, color),
          .shaderLocation = 3 },
        { .format = WGPUVertexFormat_Float32,
          .offset = offsetof(flecs_particle_instance_t, emissive),
          .shaderLocation = 4 },
        { .format = WGPUVertexFormat_Float32x3,
          .offset = offsetof(flecs_particle_instance_t, axis),
          .shaderLocation = 5 }
    };

    WGPUVertexBufferLayout vbuf_layouts[2] = {
        {
            .arrayStride = sizeof(float) * 2,
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = 1,
            .attributes = quad_attrs
        },
        {
            .arrayStride = sizeof(flecs_particle_instance_t),
            .stepMode = WGPUVertexStepMode_Instance,
            .attributeCount = 5,
            .attributes = inst_attrs
        }
    };

    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &impl->bind_layout
        });

    /* Premultiplied alpha */
    WGPUBlendState blend = {
        .color = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_One,
            .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
        },
        .alpha = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_One,
            .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
        }
    };

    WGPUColorTargetState color_target = {
        .format = color_format,
        .blend = &blend,
        .writeMask = WGPUColorWriteMask_All
    };

    WGPUDepthStencilState depth_state = {
        .format = WGPUTextureFormat_Depth32Float,
        .depthWriteEnabled = WGPUOptionalBool_False,
        .depthCompare = WGPUCompareFunction_LessEqual,
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };

    WGPURenderPipelineDescriptor desc = {
        .layout = layout,
        .vertex = {
            .module = module,
            .entryPoint = WGPU_STR("vs_main"),
            .bufferCount = 2,
            .buffers = vbuf_layouts
        },
        .fragment = &(WGPUFragmentState){
            .module = module,
            .entryPoint = WGPU_STR("fs_main"),
            .targetCount = 1,
            .targets = &color_target
        },
        .depthStencil = &depth_state,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_None
        },
        .multisample = WGPU_MULTISAMPLE(sample_count)
    };

    impl->pipeline = wgpuDeviceCreateRenderPipeline(engine->device, &desc);
    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(module);
    if (!impl->pipeline) {
        return false;
    }

    impl->pipeline_format = color_format;
    impl->pipeline_samples = sample_count;

    if (!impl->qbuf) {
        static const float quad[12] = {
            -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
            -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f
        };
        impl->qbuf = wgpuDeviceCreateBuffer(engine->device,
            &(WGPUBufferDescriptor){
                .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                .size = sizeof(quad)
            });
        if (!impl->qbuf) {
            return false;
        }
        wgpuQueueWriteBuffer(engine->queue, impl->qbuf, 0, quad,
            sizeof(quad));
    }

    return true;
}

void flecsEngine_particles_draw(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat color_format,
    int32_t sample_count,
    int32_t instance_count)
{
    FlecsParticlesImpl *impl = ecs_singleton_ensure(
        world, FlecsParticlesImpl);

    if (!flecsEngine_particles_ensurePipeline(
        impl, engine, color_format, sample_count))
    {
        return;
    }

    if (!impl->bind_group ||
        impl->bound_uniforms != view_impl->frame_uniform_buffer)
    {
        FLECS_WGPU_RELEASE(impl->bind_group, wgpuBindGroupRelease);
        WGPUBindGroupEntry entry = {
            .binding = 0,
            .buffer = view_impl->frame_uniform_buffer,
            .size = sizeof(FlecsGpuUniforms)
        };
        impl->bind_group = wgpuDeviceCreateBindGroup(engine->device,
            &(WGPUBindGroupDescriptor){
                .layout = impl->bind_layout,
                .entries = &entry,
                .entryCount = 1
            });
        if (!impl->bind_group) {
            return;
        }
        impl->bound_uniforms = view_impl->frame_uniform_buffer;
    }

    wgpuRenderPassEncoderSetPipeline(pass, impl->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, impl->bind_group, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, impl->qbuf, 0,
        sizeof(float) * 12);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, impl->ibuf, 0,
        (uint64_t)instance_count * sizeof(flecs_particle_instance_t));
    wgpuRenderPassEncoderDraw(pass, 6, (uint32_t)instance_count, 0, 0);
}

void FlecsEngineParticlesImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineParticles);

    ecs_set_name_prefix(world, "Flecs");

    ECS_META_COMPONENT(world, FlecsParticleEnvelopeMode);
    ECS_META_COMPONENT(world, FlecsParticleSortMode);
    ECS_META_COMPONENT(world, FlecsParticleEnvelope);
    ECS_COMPONENT_DEFINE(world, FlecsParticles);
    ECS_COMPONENT_DEFINE(world, FlecsParticleEmitter);
    ECS_COMPONENT_DEFINE(world, FlecsParticleBurst);
    ECS_COMPONENT_DEFINE(world, FlecsParticleWind);
    ECS_COMPONENT_DEFINE(world, FlecsParticleGround);
    ECS_COMPONENT_DEFINE(world, FlecsParticlesImpl);

    ecs_struct(world, {
        .entity = ecs_id(FlecsParticles),
        .members = {
            { .name = "capacity", .type = ecs_id(ecs_i32_t),
              .offset = offsetof(FlecsParticles, capacity) },
            { .name = "gravity", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticles, gravity) },
            { .name = "drag", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticles, drag) },
            { .name = "emissive", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticles, emissive) },
            { .name = "sort_mode", .type = ecs_id(FlecsParticleSortMode),
              .offset = offsetof(FlecsParticles, sort_mode) },
            { .name = "size_envelope", .type = ecs_id(FlecsParticleEnvelope),
              .offset = offsetof(FlecsParticles, size_envelope) },
            { .name = "alpha_envelope", .type = ecs_id(FlecsParticleEnvelope),
              .offset = offsetof(FlecsParticles, alpha_envelope) },
            { .name = "emissive_envelope",
              .type = ecs_id(FlecsParticleEnvelope),
              .offset = offsetof(FlecsParticles, emissive_envelope) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsParticleEmitter),
        .members = {
            { .name = "pool", .type = ecs_id(ecs_entity_t),
              .offset = offsetof(FlecsParticleEmitter, pool) },
            { .name = "rate", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleEmitter, rate) },
            { .name = "size", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleEmitter, size) },
            { .name = "size_variance", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleEmitter, size_variance) },
            { .name = "grow", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleEmitter, grow) },
            { .name = "stretch", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleEmitter, stretch) },
            { .name = "max_age", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleEmitter, max_age) },
            { .name = "max_age_variance", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleEmitter, max_age_variance) },
            { .name = "color", .type = ecs_id(FlecsRgba),
              .offset = offsetof(FlecsParticleEmitter, color) },
            { .name = "velocity", .type = ecs_id(flecs_vec3_t),
              .offset = offsetof(FlecsParticleEmitter, velocity) },
            { .name = "velocity_variance", .type = ecs_id(flecs_vec3_t),
              .offset = offsetof(FlecsParticleEmitter, velocity_variance) },
            { .name = "offset", .type = ecs_id(flecs_vec3_t),
              .offset = offsetof(FlecsParticleEmitter, offset) },
            { .name = "spread", .type = ecs_id(flecs_vec3_t),
              .offset = offsetof(FlecsParticleEmitter, spread) },
            { .name = "radial", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleEmitter, radial) },
            { .name = "mass", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleEmitter, mass) },
            { .name = "enabled", .type = ecs_id(ecs_bool_t),
              .offset = offsetof(FlecsParticleEmitter, enabled) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsParticleBurst),
        .members = {
            { .name = "pool", .type = ecs_id(ecs_entity_t),
              .offset = offsetof(FlecsParticleBurst, pool) },
            { .name = "count", .type = ecs_id(ecs_i32_t),
              .offset = offsetof(FlecsParticleBurst, count) },
            { .name = "size", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleBurst, size) },
            { .name = "size_variance", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleBurst, size_variance) },
            { .name = "grow", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleBurst, grow) },
            { .name = "stretch", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleBurst, stretch) },
            { .name = "max_age", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleBurst, max_age) },
            { .name = "max_age_variance", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleBurst, max_age_variance) },
            { .name = "color", .type = ecs_id(FlecsRgba),
              .offset = offsetof(FlecsParticleBurst, color) },
            { .name = "velocity", .type = ecs_id(flecs_vec3_t),
              .offset = offsetof(FlecsParticleBurst, velocity) },
            { .name = "velocity_variance", .type = ecs_id(flecs_vec3_t),
              .offset = offsetof(FlecsParticleBurst, velocity_variance) },
            { .name = "offset", .type = ecs_id(flecs_vec3_t),
              .offset = offsetof(FlecsParticleBurst, offset) },
            { .name = "spread", .type = ecs_id(flecs_vec3_t),
              .offset = offsetof(FlecsParticleBurst, spread) },
            { .name = "radial", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleBurst, radial) },
            { .name = "mass", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleBurst, mass) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsParticleWind),
        .members = {
            { .name = "pool", .type = ecs_id(ecs_entity_t),
              .offset = offsetof(FlecsParticleWind, pool) },
            { .name = "strength", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleWind, strength) },
            { .name = "width", .type = ecs_id(ecs_i32_t),
              .offset = offsetof(FlecsParticleWind, width) },
            { .name = "height", .type = ecs_id(ecs_i32_t),
              .offset = offsetof(FlecsParticleWind, height) },
            { .name = "cell_size", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleWind, cell_size) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsParticleGround),
        .members = {
            { .name = "pool", .type = ecs_id(ecs_entity_t),
              .offset = offsetof(FlecsParticleGround, pool) },
            { .name = "width", .type = ecs_id(ecs_i32_t),
              .offset = offsetof(FlecsParticleGround, width) },
            { .name = "depth", .type = ecs_id(ecs_i32_t),
              .offset = offsetof(FlecsParticleGround, depth) },
            { .name = "cell_size", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsParticleGround, cell_size) },
            { .name = "origin", .type = ecs_id(flecs_vec2_t),
              .offset = offsetof(FlecsParticleGround, origin) }
        }
    });

    ecs_set_hooks(world, FlecsParticles, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsParticles),
        .dtor = ecs_dtor(FlecsParticles)
    });

    ecs_set_hooks(world, FlecsParticlesImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsParticlesImpl),
        .dtor = ecs_dtor(FlecsParticlesImpl)
    });

    ecs_singleton_set(world, FlecsParticlesImpl, {0});

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "FlecsParticlesUpdate",
            .add = ecs_ids(ecs_dependson(EcsOnUpdate))
        }),
        .query.terms = {
            { .id = ecs_id(FlecsParticles) }
        },
        .callback = FlecsParticlesUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "FlecsParticleEmitterUpdate",
            .add = ecs_ids(ecs_dependson(EcsOnUpdate))
        }),
        .query.terms = {
            { .id = ecs_id(FlecsParticleEmitter) },
            { .id = ecs_id(FlecsWorldTransform3), .inout = EcsIn,
              .oper = EcsOptional }
        },
        .callback = FlecsParticleEmitterUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "FlecsParticleBurstUpdate",
            .add = ecs_ids(ecs_dependson(EcsOnUpdate))
        }),
        .query.terms = {
            { .id = ecs_id(FlecsParticleBurst) },
            { .id = ecs_id(FlecsWorldTransform3), .inout = EcsIn,
              .oper = EcsOptional }
        },
        .callback = FlecsParticleBurstUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "FlecsParticleWindUpdate",
            .add = ecs_ids(ecs_dependson(EcsOnUpdate))
        }),
        .query.terms = {
            { .id = ecs_id(FlecsParticleWind) }
        },
        .callback = FlecsParticleWindUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "FlecsParticleGroundUpdate",
            .add = ecs_ids(ecs_dependson(EcsOnUpdate))
        }),
        .query.terms = {
            { .id = ecs_id(FlecsParticleGround), .inout = EcsIn }
        },
        .callback = FlecsParticleGroundUpdate
    });
}
