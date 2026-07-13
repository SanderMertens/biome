#ifndef FLECS_TERRAIN_H
#define FLECS_TERRAIN_H

#undef ECS_META_IMPL
#ifndef BIOME_TERRAIN_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define TerrainCellSize (5)         /* meters */

#define TerrainSlopeIndex (0)       /* flecs_vec2_t (height gradient of tile) */
#define TerrainSoilIndex (1)        /* TerrainSoil */
#define TerrainGroundIndex (2)      /* WeatherGround */
#define TerrainAirIndex (3)
#define TerrainOccupancyIndex (4)
#define TerrainPowerIndex (5)
#define TerrainWeatherScale (4)

ECS_STRUCT(TerrainSoil, {
    float sedimentFactor;          /* 0: solid rock, 1: fine sand */
    float fertility;               /* 0: no nutrients, 1: maximum nutrients */
});

ECS_STRUCT(TerrainOccupancy, {
    uint64_t buildings;
});

/* Scatter prefab across terrain in unoccupied cells */
ECS_STRUCT(TerrainScatter,  {
    ecs_entity_t prefab;
    int32_t count;
    flecs_vec2_t position_variance; /* +/- x/z offset; 0 disables per axis */
    flecs_vec2_t rotation_variance; /* +/- x/y angle; 0 disables per axis */
    flecs_vec3_t scale_variance;    /* +/- prefab scale; 0 disables per axis */
    float cluster;                  /* 0: random, 1: fully clustered */
    float cluster_scale;            /* approximate cluster size in cells */
});

ECS_STRUCT(Terrain, {
    int16_t width;
    int16_t height;
    float scale;
    int16_t octaves;
    int16_t warp_octaves;
    float ridge_freq;
    float ridge_min;
    float ridge_octaves;
    float ridge_mix;
    float height_offset;
    float height_gain;
    float height_base;
    float max_height;
    float warp;
});

float biomeHash2(int32_t x, int32_t z);
float biomeNoise2(float x, float z);
float biomeFbm2(float x, float z, int32_t octaves);

void biomeTerrainImport(ecs_world_t *world);

#endif
