#ifndef FLECS_ENGINE_IBL_H
#define FLECS_ENGINE_IBL_H

void flecsEngine_ibl_register(
    ecs_world_t *world);

bool flecsEngine_ibl_initResources(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl,
    const char *hdri_path,
    uint32_t filter_sample_count,
    uint32_t lut_sample_count);

#endif
