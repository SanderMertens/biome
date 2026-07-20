#ifndef FLECS_ENGINE_PARTICLES_H
#define FLECS_ENGINE_PARTICLES_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_PARTICLES_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* CPU-simulated, GPU-instanced particles.
 *
 * One pool entity owns a pool of particles; the whole world's live
 * particles are drawn with a single instanced draw call into the main
 * 3D pass (depth tested against the scene, no depth writes). There is
 * no entity per particle.
 *
 * Particles are round soft sprites billboarded toward the camera; a
 * stretch > 1 turns the sprite into a vertical streak (rain). A nonzero
 * axis orients the streak along that world-space direction instead
 * (line geometry, e.g. wind). Particles can be spawned three ways, all
 * of which end up calling
 * flecsEngine_particlesEmit:
 * - C game code calling flecsEngine_particlesEmit directly.
 * - FlecsParticleEmitter, for continuous, script-declarable emission.
 * - FlecsParticleBurst, for a one-shot script-declarable burst.
 * Game systems may also steer particles each frame by mutating the pool
 * array in the FlecsParticles component directly. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flecs_particle_t {
    float pos[3];
    float vel[3];
    float axis[3];
    float size;      /* world-space sprite width */
    float grow;      /* size change per second */
    float stretch;   /* height/width ratio; <= 1 is a round sprite */
    float age;
    float max_age;
    float mass;
    FlecsRgba color;
} flecs_particle_t;

ECS_ENUM(FlecsParticleEnvelopeMode, {
    FlecsParticleEnvelopeNone,
    FlecsParticleEnvelopeLinear,
    FlecsParticleEnvelopeEaseIn,
    FlecsParticleEnvelopeEaseOut,
    FlecsParticleEnvelopeEaseInOut,
});

ECS_ENUM(FlecsParticleSortMode, {
    FlecsParticleSortBackToFront,
    FlecsParticleSortNone,
});

ECS_STRUCT(FlecsParticleEnvelope, {
    FlecsParticleEnvelopeMode mode;
    float fade_in;
    float fade_out;
});

/* capacity/gravity/drag/emissive and the envelopes are the only
 * script-settable fields; particles and count are owned by the module
 * and are not reflected. */
ECS_STRUCT(FlecsParticles, {
    int32_t capacity;   /* pool size; 0 selects the default (256) */
    float gravity;      /* downward acceleration on every particle */
    float drag;         /* fraction of velocity shed per second */
    float emissive;     /* self-illumination strength */
    FlecsParticleSortMode sort_mode;

    FlecsParticleEnvelope size_envelope;
    FlecsParticleEnvelope alpha_envelope;
    FlecsParticleEnvelope emissive_envelope;

    /* Managed by the module */
    flecs_particle_t *particles;
    int32_t count;

    /* Cumulative stats, managed by the module */
    int32_t stat_emitted;
    int32_t stat_dropped;
    int32_t stat_ground;
    int32_t stat_expired;
});

extern ECS_COMPONENT_DECLARE(FlecsParticleEnvelopeMode);
extern ECS_COMPONENT_DECLARE(FlecsParticleSortMode);
extern ECS_COMPONENT_DECLARE(FlecsParticleEnvelope);
extern ECS_COMPONENT_DECLARE(FlecsParticles);

/* Continuous emitter: spawns rate particles/sec at the entity's world
 * position (from FlecsWorldTransform3, if present) plus offset. Disabled
 * or rate <= 0 emits nothing. pool == 0 emits into a FlecsParticles pool
 * on the same entity (added automatically if missing).
 *
 * size/max_age/velocity are randomized per spawned particle within
 * +-variance (independently per axis for velocity). Since components are
 * copied on instantiation by default, many entities can share one setup
 * via an IsA prefab and still emit and accumulate independently. */
ECS_STRUCT(FlecsParticleEmitter, {
    ecs_entity_t pool;
    float rate;
    float size;
    float size_variance;
    float grow;
    float stretch;
    float max_age;
    float max_age_variance;
    FlecsRgba color;
    flecs_vec3_t velocity;
    flecs_vec3_t velocity_variance;
    flecs_vec3_t offset;  /* local offset from the entity's world position */
    flecs_vec3_t spread;
    float radial;
    float mass;
    bool enabled;

    /* Managed by the module: fractional particle carried to the next tick */
    float accum;
});

extern ECS_COMPONENT_DECLARE(FlecsParticleEmitter);

/* One-shot burst: setting FlecsParticleBurst emits count particles at the
 * entity's world position + offset on the next OnUpdate tick, after which
 * the component removes itself. Fields have the same meaning as on
 * FlecsParticleEmitter, minus rate/enabled. */
ECS_STRUCT(FlecsParticleBurst, {
    ecs_entity_t pool;
    int32_t count;
    float size;
    float size_variance;
    float grow;
    float stretch;
    float max_age;
    float max_age_variance;
    FlecsRgba color;
    flecs_vec3_t velocity;
    flecs_vec3_t velocity_variance;
    flecs_vec3_t offset;
    flecs_vec3_t spread;
    float radial;
    float mass;
});

extern ECS_COMPONENT_DECLARE(FlecsParticleBurst);

void flecsEngine_particlesBurst(
    ecs_world_t *world,
    ecs_entity_t pool,
    const float pos[3],
    const FlecsParticleBurst *burst);

ECS_STRUCT(FlecsParticleWind, {
    ecs_entity_t pool;
    float strength;
    int32_t width;
    int32_t height;
    float cell_size;
    const flecs_vec2_t *field;
});

extern ECS_COMPONENT_DECLARE(FlecsParticleWind);

/* Height map collision: kills particles in the pool when they reach the
 * ground. corner_heights holds (width+1)*(depth+1) corner heights (the
 * FlecsTerrain layout); origin is the world xz of corner (0,0). pool == 0
 * targets a FlecsParticles pool on the same entity. */
ECS_STRUCT(FlecsParticleGround, {
    ecs_entity_t pool;
    int32_t width;
    int32_t depth;
    float cell_size;
    flecs_vec2_t origin;
    const float *corner_heights;
});

extern ECS_COMPONENT_DECLARE(FlecsParticleGround);

/* Add a particle to a pool. When the pool is full the particle is dropped.
 * Fields left zero get usable defaults (stretch 1, max_age 1, size 0.1,
 * opaque white). */
void flecsEngine_particlesEmit(
    ecs_world_t *world,
    ecs_entity_t pool,
    const flecs_particle_t *particle);

void FlecsEngineParticlesImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif
