#define BIOME_WEATHER_IMPL

#include "biome.h"

#define WeatherTimeScale (10.0f)
#define WeatherFixedDeltaTime (0.016f)
#define WeatherUpdateInterval (3)
#define WeatherSpaceTemperature (-180.0f)
#define WeatherSunHeatRate (0.15f)
#define WeatherMaxSunIntensity (10.0f)
#define WeatherGroundRadiationRate (0.2f)
#define WeatherAirRadiationRate (0.07f)
#define WeatherGreenhousePerCo2 (8.0f)
#define WeatherGreenhousePerVapor (0.15f)
#define WeatherGreenhousePerCloud (1.5f)
#define WeatherCloudReflect (0.35f)
#define WeatherCloudHalfCover (0.5f)
#define WeatherGroundDiffusionRate (0.05f)
#define WeatherAirDiffusionRate (0.05f)
#define WeatherGroundExchangeRate (0.02f)
#define WeatherAirExchangeRate (0.08f)
#define WeatherWindTempResponse (30.0f)
#define WeatherWindRelaxRate (0.5f)
#define WeatherMaxWind (8.0f)
#define WeatherAdvectMaxFraction (0.2f)
#define WeatherUpdraftResponse (1.0f)
#define WeatherSaturationBase (0.18f)
#define WeatherSaturationScale (12.0f)
#define WeatherCondenseRate (0.3f)
#define WeatherUpdraftCondense (0.5f)
#define WeatherLatentHeat (4.0f)
#define WeatherRainThreshold (0.1f)
#define WeatherRainRate (0.1f)
#define WeatherEvapWaterRate (0.05f)
#define WeatherEvapMoistureRate (0.01f)
#define WeatherEvapSandResist (0.75f)
#define WeatherMoistureEvapHumidityMax (0.5f)
#define WeatherMoistureLeakRate (0.05f)
#define WeatherEvapCooling (2.0f)
#define WeatherInfiltrationRate (0.1f)
#define WeatherMoistureCapacity (1.0f)
#define WeatherMoistureCapacityMin (0.25f)
#define WeatherFreezeRate (0.05f)
#define WeatherFreezeRange (4.0f)
#define WeatherSublimationRate (0.02f)
#define WeatherWaterFlowRate (0.4f)
#define WeatherWaterHeadScale (1.0f)
#define WeatherIceNoiseFrequency (0.045f)
#define WeatherIceThreshold (0.68f)
#define WeatherIceWarp (2.5f)

static float weather_clamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int32_t weather_terrainCoord(
    int32_t coordinate,
    int32_t scale,
    int32_t extent)
{
    int32_t result = coordinate * scale + scale / 2;
    return result < extent ? result : extent - 1;
}

static int32_t weather_terrainIndex(
    const FlecsTerrain *t,
    int32_t x,
    int32_t z,
    int32_t scale)
{
    int32_t tx = weather_terrainCoord(x, scale, t->width);
    int32_t tz = weather_terrainCoord(z, scale, t->depth);
    return tz * t->width + tx;
}

static float weather_terrainHeight(
    const FlecsTerrain *t,
    int32_t x,
    int32_t z,
    int32_t scale)
{
    return flecsEngine_terrainCellHeight(t,
        weather_terrainCoord(x, scale, t->width),
        weather_terrainCoord(z, scale, t->depth));
}

static float weather_saturation(float temperature) {
    return WeatherSaturationBase * expf(temperature / WeatherSaturationScale);
}

static float weather_airFrac(float height, float atm_height) {
    if (atm_height <= 0) {
        return 0;
    }
    return weather_clamp((atm_height - height) / atm_height, 0, 1.0f);
}

static float weather_jitter(
    int32_t x,
    int32_t z,
    int32_t salt,
    float variation)
{
    float h = biomeHash2(x + salt * 7919, z - salt * 15731);
    return 1.0f + (h - 0.5f) * 2.0f * variation;
}

static float weather_iceMask(int32_t x, int32_t z) {
    float wx = (float)x * WeatherIceNoiseFrequency;
    float wz = (float)z * WeatherIceNoiseFrequency;
    float qx = biomeFbm2(wx * 2.0f + 31.4f, wz * 2.0f + 7.7f, 2) - 0.5f;
    float qz = biomeFbm2(wx * 2.0f + 53.9f, wz * 2.0f + 91.2f, 2) - 0.5f;
    float n = biomeFbm2(
        wx + WeatherIceWarp * qx + 3.1f,
        wz + WeatherIceWarp * qz + 17.9f, 3);
    if (n <= WeatherIceThreshold) {
        return 0;
    }
    return (n - WeatherIceThreshold) / (1.0f - WeatherIceThreshold);
}

static float weather_moistureCapacity(const TerrainSoil *s) {
    float sed = weather_clamp(s->sedimentFactor, 0, 1.0f);
    return WeatherMoistureCapacity * (WeatherMoistureCapacityMin +
        (1.0f - WeatherMoistureCapacityMin) * sed);
}

static float weather_albedo(
    const WeatherGroundTile *g,
    const TerrainSoil *s)
{
    float base = 0.18f + 0.17f * weather_clamp(s->sedimentFactor, 0, 1.0f);
    base -= 0.08f * weather_clamp(g->moisture, 0, 1.0f);
    float ice = g->water * weather_clamp(g->frozen, 0, 1.0f);
    float ice_cover = ice / (ice + 0.08f);
    return weather_clamp(base + (0.8f - base) * ice_cover, 0.05f, 0.9f);
}

static float weather_sun(
    const ecs_world_t *world,
    const WeatherConfig *c,
    float sun_dir[3])
{
    sun_dir[0] = 0;
    sun_dir[1] = 1.0f;
    sun_dir[2] = 0;

    float intensity = 1.0f;
    if (!c->sun || !ecs_is_alive(world, c->sun)) {
        return intensity;
    }

    const FlecsDirectionalLight *dl = ecs_get(
        world, c->sun, FlecsDirectionalLight);
    if (dl) {
        intensity = weather_clamp(dl->intensity, 0, WeatherMaxSunIntensity);
    }

    const FlecsRotation3 *rot = ecs_get(world, c->sun, FlecsRotation3);
    if (rot) {
        float pitch = rot->x, yaw = rot->y;
        float x = -sinf(yaw) * cosf(pitch);
        float y = -sinf(pitch);
        float z = -cosf(yaw) * cosf(pitch);
        float len = sqrtf(x * x + y * y + z * z);
        if (len > 1e-6f) {
            sun_dir[0] = x / len;
            sun_dir[1] = y / len;
            sun_dir[2] = z / len;
        }
    }

    return intensity;
}

static void weather_airUpdate(
    const WeatherAirTile *old_tiles,
    WeatherAirTile *new_tiles,
    const FlecsTerrain *t,
    float atm_height,
    int32_t w,
    int32_t d,
    int32_t scale,
    float cs,
    float dt,
    float opacity_base)
{
    float blend = weather_clamp(WeatherWindRelaxRate * dt, 0, 1.0f);

    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = z * w + x;
            float t_old = old_tiles[i].temperature;

            float opacity = (opacity_base +
                WeatherGreenhousePerVapor * old_tiles[i].vapor +
                WeatherGreenhousePerCloud * old_tiles[i].cloud_water) *
                weather_airFrac(
                    weather_terrainHeight(t, x, z, scale), atm_height);
            float rel = (t_old - WeatherSpaceTemperature) * 0.01f;
            if (rel < 0) rel = 0;
            new_tiles[i].temperature -= dt * WeatherAirRadiationRate *
                rel * rel / (1.0f + opacity);

            int32_t xm = x > 0 ? x - 1 : w - 1;
            int32_t xp = x < w - 1 ? x + 1 : 0;
            int32_t zm = z > 0 ? z - 1 : d - 1;
            int32_t zp = z < d - 1 ? z + 1 : 0;

            float t_sum = old_tiles[z * w + xm].temperature +
                old_tiles[z * w + xp].temperature +
                old_tiles[zm * w + x].temperature +
                old_tiles[zp * w + x].temperature;

            float t_avg = t_sum * 0.25f;
            new_tiles[i].temperature += dt * WeatherAirDiffusionRate *
                (t_sum - 4.0f * t_old);

            float dtdx = (old_tiles[z * w + xp].temperature -
                old_tiles[z * w + xm].temperature) / (2.0f * cs);
            float dtdz = (old_tiles[zp * w + x].temperature -
                old_tiles[zm * w + x].temperature) / (2.0f * cs);

            float target_u = weather_clamp(WeatherWindTempResponse * dtdx,
                -WeatherMaxWind, WeatherMaxWind);
            float target_v = weather_clamp(WeatherWindTempResponse * dtdz,
                -WeatherMaxWind, WeatherMaxWind);

            float u = old_tiles[i].wind_velocity.x;
            float v = old_tiles[i].wind_velocity.z;
            new_tiles[i].wind_velocity.x = u + (target_u - u) * blend;
            new_tiles[i].wind_velocity.z = v + (target_v - v) * blend;
            new_tiles[i].wind_velocity.y = weather_clamp(
                WeatherUpdraftResponse * (t_old - t_avg),
                -WeatherMaxWind, WeatherMaxWind);
        }
    }

    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = z * w + x;
            int32_t j = z * w + (x < w - 1 ? x + 1 : 0);
            float u = 0.5f * (new_tiles[i].wind_velocity.x +
                new_tiles[j].wind_velocity.x);
            if (u < 0.02f && u > -0.02f) continue;
            float f = weather_clamp(
                (u > 0 ? u : -u) * dt / cs, 0, WeatherAdvectMaxFraction);
            int32_t donor = u > 0 ? i : j;
            int32_t recv = u > 0 ? j : i;
            float dv = f * old_tiles[donor].vapor;
            float dc = f * old_tiles[donor].cloud_water;
            new_tiles[donor].vapor -= dv;
            new_tiles[recv].vapor += dv;
            new_tiles[donor].cloud_water -= dc;
            new_tiles[recv].cloud_water += dc;
            float dtemp = f * (old_tiles[donor].temperature -
                old_tiles[recv].temperature);
            new_tiles[recv].temperature += dtemp;
            new_tiles[donor].temperature -= dtemp;
        }
    }

    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = z * w + x;
            int32_t j = (z < d - 1 ? z + 1 : 0) * w + x;
            float v = 0.5f * (new_tiles[i].wind_velocity.z +
                new_tiles[j].wind_velocity.z);
            if (v < 0.02f && v > -0.02f) continue;
            float f = weather_clamp(
                (v > 0 ? v : -v) * dt / cs, 0, WeatherAdvectMaxFraction);
            int32_t donor = v > 0 ? i : j;
            int32_t recv = v > 0 ? j : i;
            float dv = f * old_tiles[donor].vapor;
            float dc = f * old_tiles[donor].cloud_water;
            new_tiles[donor].vapor -= dv;
            new_tiles[recv].vapor += dv;
            new_tiles[donor].cloud_water -= dc;
            new_tiles[recv].cloud_water += dc;
            float dtemp = f * (old_tiles[donor].temperature -
                old_tiles[recv].temperature);
            new_tiles[recv].temperature += dtemp;
            new_tiles[donor].temperature -= dtemp;
        }
    }
}

static void weather_airMixEdge(
    WeatherAirTile *tiles,
    int32_t i,
    int32_t j,
    float mix)
{
    float dv = tiles[i].vapor - tiles[j].vapor;
    float sv = tiles[i].vapor + tiles[j].vapor;
    if (sv > 1e-6f) {
        float amount = 0.5f * dv * (fabsf(dv) / sv) * mix;
        tiles[i].vapor -= amount;
        tiles[j].vapor += amount;
    }

    float dc = tiles[i].cloud_water - tiles[j].cloud_water;
    float sc = tiles[i].cloud_water + tiles[j].cloud_water;
    if (sc > 1e-6f) {
        float amount = 0.5f * dc * (fabsf(dc) / sc) * mix;
        tiles[i].cloud_water -= amount;
        tiles[j].cloud_water += amount;
    }
}

static void weather_airMix(
    WeatherAirTile *tiles,
    int32_t w,
    int32_t d,
    float dt)
{
    float mix = weather_clamp(dt, 0, 1.0f);

    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = z * w + x;
            weather_airMixEdge(tiles, i, z * w + (x < w - 1 ? x + 1 : 0),
                mix);
        }
    }
    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = z * w + x;
            weather_airMixEdge(tiles, i, (z < d - 1 ? z + 1 : 0) * w + x,
                mix);
        }
    }
}

static void weather_airCondense(
    WeatherAirTile *tiles,
    int32_t cells,
    float dt)
{
    float rate = weather_clamp(WeatherCondenseRate * dt, 0, 1.0f);
    float rain_rate = weather_clamp(WeatherRainRate * dt, 0, 1.0f);

    for (int32_t i = 0; i < cells; i ++) {
        WeatherAirTile *a = &tiles[i];

        float excess = a->vapor - weather_saturation(a->temperature);
        float r = rate;
        if (excess > 0) {
            float updraft = a->wind_velocity.y > 0 ? a->wind_velocity.y : 0;
            r = weather_clamp(
                rate * (1.0f + WeatherUpdraftCondense * updraft), 0, 1.0f);
        }
        float dc = excess * r;
        if (dc < -a->cloud_water) {
            dc = -a->cloud_water;
        }
        a->vapor -= dc;
        a->cloud_water += dc;
        a->temperature += dc * WeatherLatentHeat;

        float drop = 0;
        if (a->cloud_water > WeatherRainThreshold) {
            drop = (a->cloud_water - WeatherRainThreshold) * rain_rate;
            a->cloud_water -= drop;
        }
        a->precipitation = drop;
    }
}

static void weather_waterFlowEdge(
    WeatherGroundTile *ground,
    const float *head,
    int32_t i,
    int32_t j,
    float rate)
{
    float dh = head[i] - head[j];
    int32_t donor = dh > 0 ? i : j;
    int32_t recv = dh > 0 ? j : i;
    WeatherGroundTile *gd = &ground[donor];

    float flowable = gd->water *
        (1.0f - weather_clamp(gd->frozen, 0, 1.0f));
    float amount = (dh > 0 ? dh : -dh) *
        (rate * 0.5f / WeatherWaterHeadScale);
    amount = fminf(amount, flowable);
    if (amount <= 0) {
        return;
    }

    gd->water -= amount;
    ground[recv].water += amount;
}

static void weather_waterFlow(
    const FlecsTerrain *t,
    const WeatherGroundTile *old_ground,
    WeatherGroundTile *new_ground,
    float *head,
    int32_t w,
    int32_t d,
    int32_t scale,
    float dt)
{
    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = z * w + x;
            head[i] = weather_terrainHeight(t, x, z, scale) +
                old_ground[i].water * WeatherWaterHeadScale;
        }
    }

    float rate = weather_clamp(WeatherWaterFlowRate * dt, 0, 1.0f);

    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w - 1; x ++) {
            int32_t i = z * w + x;
            weather_waterFlowEdge(new_ground, head, i, i + 1, rate);
        }
    }
    for (int32_t z = 0; z < d - 1; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = z * w + x;
            weather_waterFlowEdge(new_ground, head, i, i + w, rate);
        }
    }
}

static void weather_seed(
    const WeatherConfig *c,
    const FlecsTerrain *t,
    const TerrainSoil *soil,
    WeatherGroundTile *ground,
    WeatherAirTile *air,
    int32_t w,
    int32_t d,
    int32_t scale)
{
    float variation = c->seed_variation;

    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = z * w + x;
            int32_t ti = weather_terrainIndex(t, x, z, scale);
            int32_t wx = weather_terrainCoord(x, scale, t->width);
            int32_t wz = weather_terrainCoord(z, scale, t->depth);
            WeatherGroundTile *g = &ground[i];

            *g = c->seed_ground;
            g->temperature *= weather_jitter(wx, wz, 1, variation);
            g->moisture *= weather_jitter(wx, wz, 2, variation);
            g->water *= weather_jitter(wx, wz, 3, variation);
            g->frozen = weather_clamp(g->frozen * weather_iceMask(wx, wz) *
                weather_jitter(wx, wz, 4, variation), 0, 1.0f);
            g->albedo = weather_albedo(g, &soil[ti]);

            WeatherAirTile *a = &air[i];
            *a = c->seed_air;
            a->temperature *= weather_jitter(wx, wz, 10, variation);
            a->vapor *= weather_jitter(wx, wz, 11, variation);
        }
    }
}

void WeatherInit(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];

    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    const WeatherConfig *c = ecs_field(it, WeatherConfig, 1);
    WeatherBuffers *b = ecs_field(it, WeatherBuffers, 2);

    int32_t w, d;
    flecsEngine_terrainLayerDimensions(t, TerrainGroundIndex, &w, &d);
    if (w <= 0 || d <= 0) {
        return;
    }
    if (ecs_vec_count(&t->layerTypes) <= TerrainAirIndex) {
        return;
    }

    const TerrainSoil *soil = flecsEngine_terrain_getLayer(world, e, TerrainSoilIndex, TerrainSoil);
    WeatherGroundTile *ground = flecsEngine_terrain_getLayer(world, e, TerrainGroundIndex, WeatherGroundTile);
    WeatherAirTile *air = flecsEngine_terrain_getLayer(world, e, TerrainAirIndex, WeatherAirTile);

    if (!soil || !ground || !air) {
        return;
    }

    int32_t air_w, air_d;
    flecsEngine_terrainLayerDimensions(t, TerrainAirIndex, &air_w, &air_d);
    int32_t scale = flecsEngine_terrainLayerScale(t, TerrainGroundIndex);
    if (air_w != w || air_d != d ||
        flecsEngine_terrainLayerScale(t, TerrainAirIndex) != scale)
    {
        ecs_err("weather ground and air layers must have the same scale");
        return;
    }
    weather_seed(c, t, soil, ground, air, w, d, scale);
    b->width = w;
    b->depth = d;
}

void WeatherUpdate(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];

    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    WeatherAtmosphere *atm = ecs_field(it, WeatherAtmosphere, 1);
    const WeatherConfig *c = ecs_field(it, WeatherConfig, 2);
    WeatherBuffers *b = ecs_field(it, WeatherBuffers, 3);

    int32_t w, d;
    flecsEngine_terrainLayerDimensions(t, TerrainGroundIndex, &w, &d);
    if (w <= 0 || d <= 0) {
        return;
    }
    if (ecs_vec_count(&t->layerTypes) <= TerrainAirIndex) {
        return;
    }

    const flecs_vec2_t *slope = flecsEngine_terrain_getLayer(world, e, TerrainSlopeIndex, flecs_vec2_t);
    const TerrainSoil *soil = flecsEngine_terrain_getLayer(world, e, TerrainSoilIndex, TerrainSoil);
    WeatherGroundTile *ground = flecsEngine_terrain_getLayer(world, e, TerrainGroundIndex, WeatherGroundTile);
    WeatherAirTile *air = flecsEngine_terrain_getLayer(world, e, TerrainAirIndex, WeatherAirTile);

    if (!slope || !soil || !ground || !air) {
        return;
    }

    int32_t air_w, air_d;
    flecsEngine_terrainLayerDimensions(t, TerrainAirIndex, &air_w, &air_d);
    int32_t scale = flecsEngine_terrainLayerScale(t, TerrainGroundIndex);
    if (air_w != w || air_d != d ||
        flecsEngine_terrainLayerScale(t, TerrainAirIndex) != scale)
    {
        ecs_err("weather ground and air layers must have the same scale");
        return;
    }

    int32_t cells = w * d;
    float cs = (t->cell_size > 0 ? t->cell_size : 1.0f) * scale;

    float terrain_surface = (float)t->width * (float)t->depth *
        t->cell_size * t->cell_size;
    float atmosphere_volume = terrain_surface * atm->atmosphere_height;
    float co2_content = atmosphere_volume > 0
        ? atm->co2_quantity / atmosphere_volume
        : 0;
    float opacity_base = WeatherGreenhousePerCo2 *
        weather_clamp(co2_content, 0, 1.0f);

    if (ecs_vec_count(&b->ground_buffer) != cells) {
        ecs_vec_set_count_t(NULL, &b->ground_buffer, WeatherGroundTile, cells);
        ecs_vec_set_count_t(NULL, &b->air_buffer, WeatherAirTile, cells);
        ecs_vec_set_count_t(NULL, &b->head_buffer, float, cells);
    }

    if (b->width != w || b->depth != d) {
        weather_seed(c, t, soil, ground, air, w, d, scale);
        b->width = w;
        b->depth = d;
    }

    if (++ b->frame_skip < WeatherUpdateInterval) {
        return;
    }
    b->frame_skip = 0;

    float dt = WeatherFixedDeltaTime * WeatherUpdateInterval *
        WeatherTimeScale;

    ecs_time_t update_timer;
    ecs_time_measure(&update_timer);

    WeatherGroundTile *ground_new = ecs_vec_first_t(
        &b->ground_buffer, WeatherGroundTile);
    WeatherAirTile *air_new = ecs_vec_first_t(&b->air_buffer, WeatherAirTile);

    ecs_os_memcpy_n(ground_new, ground, WeatherGroundTile, cells);
    ecs_os_memcpy_n(air_new, air, WeatherAirTile, cells);

    float atm_h = atm->atmosphere_height;

    weather_airUpdate(air, air_new, t, atm_h, w, d, scale, cs, dt,
        opacity_base);
    weather_airMix(air_new, w, d, dt);
    weather_airCondense(air_new, cells, dt);

    float sun_dir[3];
    float sun_intensity = weather_sun(world, c, sun_dir);

    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = z * w + x;
            int32_t ti = weather_terrainIndex(t, x, z, scale);
            WeatherGroundTile *g = &ground_new[i];
            float t_old = ground[i].temperature;

            float cloud_col = air[i].cloud_water;
            float vapor_col = air[i].vapor;

            float reflect = WeatherCloudReflect * cloud_col /
                (cloud_col + WeatherCloudHalfCover);
            float sun_cell = sun_intensity * (1.0f - reflect);

            float sx = slope[ti].x, sz = slope[ti].y;
            float inv_len = 1.0f / sqrtf(sx * sx + sz * sz + 1.0f);
            float ndotl = (sun_dir[1] - sx * sun_dir[0] - sz * sun_dir[2]) *
                inv_len;
            if (ndotl < 0) ndotl = 0;
            g->temperature += dt * WeatherSunHeatRate * sun_cell *
                ndotl * (1.0f - ground[i].albedo);

            float opacity = (opacity_base +
                WeatherGreenhousePerVapor * vapor_col +
                WeatherGreenhousePerCloud * cloud_col) *
                weather_airFrac(
                    weather_terrainHeight(t, x, z, scale), atm_h);
            float rel = (t_old - WeatherSpaceTemperature) * 0.01f;
            if (rel < 0) rel = 0;
            g->temperature -= dt * WeatherGroundRadiationRate *
                rel * rel / (1.0f + opacity);

            float diffusion = 0;
            if (x > 0) diffusion += ground[i - 1].temperature - t_old;
            if (x < w - 1) diffusion += ground[i + 1].temperature - t_old;
            if (z > 0) diffusion += ground[i - w].temperature - t_old;
            if (z < d - 1) diffusion += ground[i + w].temperature - t_old;
            g->temperature += dt * WeatherGroundDiffusionRate * diffusion;

            float m_old = ground[i].moisture;
            float leak = 0;
            if (x > 0) leak += ground[i - 1].moisture - m_old;
            if (x < w - 1) leak += ground[i + 1].moisture - m_old;
            if (z > 0) leak += ground[i - w].moisture - m_old;
            if (z < d - 1) leak += ground[i + w].moisture - m_old;
            g->moisture += dt * WeatherMoistureLeakRate * leak;

            float air_t = air[i].temperature;
            g->temperature += dt * WeatherGroundExchangeRate *
                (air_t - t_old);
            air_new[i].temperature += dt * WeatherAirExchangeRate *
                (t_old - air_t);

            g->water += air_new[i].precipitation;

            float target = weather_clamp(
                0.5f - g->temperature / WeatherFreezeRange, 0, 1.0f);
            float frozen = weather_clamp(g->frozen, 0, 1.0f);
            frozen += (target - frozen) *
                weather_clamp(WeatherFreezeRate * dt, 0, 1.0f);
            g->frozen = frozen;

            float liquid = g->water * (1.0f - frozen);

            float capacity = weather_moistureCapacity(&soil[ti]);
            float room = capacity - g->moisture;
            if (room > 0 && liquid > 0) {
                float infil = fminf(room, WeatherInfiltrationRate *
                    weather_clamp(soil[ti].sedimentFactor, 0, 1.0f) *
                    liquid * dt);
                infil = fminf(infil, liquid);
                g->water -= infil;
                liquid -= infil;
                g->moisture += infil;
            }

            float deficit = weather_saturation(t_old) - air[i].vapor;
            if (deficit > 0) {
                float warm = weather_clamp(
                    (t_old + 10.0f) / 30.0f, 0, 1.5f);
                float evap_water = fminf(liquid,
                    deficit * WeatherEvapWaterRate * warm * dt);
                float evap_moisture = 0;
                float dry = 1.0f - air[i].vapor /
                    (weather_saturation(air_t) *
                        WeatherMoistureEvapHumidityMax);
                if (g->water <= 0 && dry > 0) {
                    float resist = 1.0f - WeatherEvapSandResist *
                        weather_clamp(soil[ti].sedimentFactor, 0, 1.0f);
                    float wetness = weather_clamp(
                        g->moisture / capacity, 0, 1.0f);
                    evap_moisture = fminf(g->moisture,
                        deficit * WeatherEvapMoistureRate * resist *
                            dry * warm * wetness * dt);
                }
                g->water -= evap_water;
                g->moisture -= evap_moisture;
                air_new[i].vapor += evap_water + evap_moisture;
                g->temperature -= (evap_water + evap_moisture) *
                    WeatherEvapCooling;

                float ice = g->water * frozen;
                if (ice > 0) {
                    float subl = fminf(ice,
                        deficit * WeatherSublimationRate * dt);
                    g->water -= subl;
                    air_new[i].vapor += subl;
                }
            }

            if (g->moisture > capacity) {
                g->water += g->moisture - capacity;
                g->moisture = capacity;
            }

            g->water = fmaxf(g->water, 0);
            g->moisture = fmaxf(g->moisture, 0);
            g->albedo = weather_albedo(g, &soil[ti]);
        }
    }

    weather_waterFlow(t, ground, ground_new,
        ecs_vec_first_t(&b->head_buffer, float), w, d, scale, dt);

    ecs_os_memcpy_n(ground, ground_new, WeatherGroundTile, cells);
    ecs_os_memcpy_n(air, air_new, WeatherAirTile, cells);

    double vapor_sum = 0, temp_sum = 0;
    int32_t terrain_cells = t->width * t->depth;
    for (int32_t z = 0; z < d; z ++) {
        int32_t span_z = t->depth - z * scale;
        if (span_z > scale) span_z = scale;
        for (int32_t x = 0; x < w; x ++) {
            int32_t span_x = t->width - x * scale;
            if (span_x > scale) span_x = scale;
            int32_t weight = span_x * span_z;
            int32_t i = z * w + x;
            vapor_sum += air[i].vapor * weight;
            temp_sum += air[i].temperature * weight;
        }
    }
    atm->vapor_content = (float)vapor_sum;
    atm->temperature = (float)(temp_sum / terrain_cells);

    b->update_time_sum += ecs_time_measure(&update_timer);
    b->update_time_count ++;

    if (!(b->debug_frame ++ % 200)) {
        double ice = 0, water = 0, moisture = 0, vapor = 0, cloud = 0;
        double precip = 0, ta = 0, tg = 0;
        double wind_sum = 0;
        float g_min = 1e9f, g_max = -1e9f;
        float a_min = 1e9f, a_max = -1e9f;
        float water_max = 0, frozen_max = 0;
        float wind_max = 0, w_min = 1e9f, w_max = -1e9f;
        float h_min = 1e9f, h_max = -1e9f;
        for (int32_t i = 0; i < cells; i ++) {
            float h = weather_terrainHeight(t, i % w, i / w, scale);
            if (h < h_min) h_min = h;
            if (h > h_max) h_max = h;
        }
        float h_mid = 0.5f * (h_min + h_max);
        double tg_lo = 0, tg_hi = 0;
        int32_t n_lo = 0, n_hi = 0;
        double precip_edge = 0;
        int32_t n_edge = 0;
        for (int32_t i = 0; i < cells; i ++) {
            ice += ground[i].water * ground[i].frozen;
            water += ground[i].water;
            moisture += ground[i].moisture;
            vapor += air[i].vapor;
            cloud += air[i].cloud_water;
            precip += air[i].precipitation;
            int32_t px = i % w, pz = i / w;
            if (!px || !pz || px == w - 1 || pz == d - 1) {
                precip_edge += air[i].precipitation;
                n_edge ++;
            }
            ta += air[i].temperature;
            tg += ground[i].temperature;
            if (ground[i].water > water_max) water_max = ground[i].water;
            if (ground[i].frozen > frozen_max) frozen_max = ground[i].frozen;
            float wx = air[i].wind_velocity.x;
            float wz = air[i].wind_velocity.z;
            float mag = sqrtf(wx * wx + wz * wz);
            wind_sum += mag;
            if (mag > wind_max) wind_max = mag;
            float wy = air[i].wind_velocity.y;
            if (wy < w_min) w_min = wy;
            if (wy > w_max) w_max = wy;
            float gt = ground[i].temperature;
            if (gt < g_min) g_min = gt;
            if (gt > g_max) g_max = gt;
            if (weather_terrainHeight(t, i % w, i / w, scale) < h_mid) {
                tg_lo += gt;
                n_lo ++;
            } else {
                tg_hi += gt;
                n_hi ++;
            }
            float at = air[i].temperature;
            if (at < a_min) a_min = at;
            if (at > a_max) a_max = at;
        }
        // printf("[weather] Tg [%.1f, %.1f] avg %.1f (lo %.1f hi %.1f) | "
        //     "Ta [%.1f, %.1f] avg %.1f | "
        //     "water %.0f (max %.1f) ice %.0f (frozen max %.2f) moist %.0f "
        //     "vapor %.0f cloud %.0f total %.1f | precip %.2f (edge %.2fx) | "
        //     "wind avg %.2f max %.2f | w [%.2f, %.2f] | upd %.3fms\n",
        //     (double)g_min, (double)g_max, tg / cells,
        //     tg_lo / (n_lo ? n_lo : 1), tg_hi / (n_hi ? n_hi : 1),
        //     (double)a_min, (double)a_max, ta / cells,
        //     water, (double)water_max, ice, (double)frozen_max, moisture,
        //     vapor, cloud,
        //     water + moisture + vapor + cloud,
        //     precip,
        //     precip > 0 && n_edge && n_edge < cells ?
        //         (precip_edge / n_edge) /
        //             (precip / cells) : 0,
        //     wind_sum / cells, (double)wind_max,
        //     (double)w_min, (double)w_max,
        //     1000.0 * b->update_time_sum /
        //         (b->update_time_count ? b->update_time_count : 1));
        b->update_time_sum = 0;
        b->update_time_count = 0;
    }
}

static void WeatherBuffers_fini(
    WeatherBuffers *ptr)
{
    ecs_vec_fini_t(NULL, &ptr->ground_buffer, WeatherGroundTile);
    ecs_vec_fini_t(NULL, &ptr->air_buffer, WeatherAirTile);
    ecs_vec_fini_t(NULL, &ptr->head_buffer, float);
}

ECS_CTOR(WeatherBuffers, ptr, {
    ecs_os_zeromem(ptr);
    ecs_vec_init_t(NULL, &ptr->ground_buffer, WeatherGroundTile, 0);
    ecs_vec_init_t(NULL, &ptr->air_buffer, WeatherAirTile, 0);
    ecs_vec_init_t(NULL, &ptr->head_buffer, float, 0);
})

ECS_MOVE(WeatherBuffers, dst, src, {
    WeatherBuffers_fini(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(WeatherBuffers, dst, src, {
    WeatherBuffers_fini(dst);
    *dst = *src;
    dst->ground_buffer = ecs_vec_copy_t(
        NULL, &src->ground_buffer, WeatherGroundTile);
    dst->air_buffer = ecs_vec_copy_t(
        NULL, &src->air_buffer, WeatherAirTile);
    dst->head_buffer = ecs_vec_copy_t(
        NULL, &src->head_buffer, float);
})

ECS_DTOR(WeatherBuffers, ptr, {
    WeatherBuffers_fini(ptr);
})

void biomeWeatherImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeWeather);

    ecs_set_name_prefix(world, "Weather");

    ECS_META_COMPONENT(world, WeatherGroundTile);
    ECS_META_COMPONENT(world, WeatherAirTile);
    ECS_META_COMPONENT(world, WeatherConfig);
    ECS_META_COMPONENT(world, WeatherAtmosphere);
    ECS_META_COMPONENT(world, WeatherBuffers);

    ecs_set_hooks(world, WeatherBuffers, {
        .ctor = ecs_ctor(WeatherBuffers),
        .move = ecs_move(WeatherBuffers),
        .copy = ecs_copy(WeatherBuffers),
        .dtor = ecs_dtor(WeatherBuffers)
    });

    ecs_add_id(world, ecs_id(WeatherAtmosphere), EcsSingleton);
    ecs_add_id(world, ecs_id(WeatherConfig), EcsSingleton);
    ecs_add_pair(world, ecs_id(Terrain), EcsWith, ecs_id(WeatherBuffers));

    ECS_SYSTEM(world, WeatherInit, EcsOnStart,
        [inout] FlecsTerrain,
        [in]    WeatherConfig,
        [inout] WeatherBuffers);

    ECS_SYSTEM(world, WeatherUpdate, EcsPostUpdate,
        [inout] FlecsTerrain,
        [inout] WeatherAtmosphere,
        [in]    WeatherConfig,
        [inout] WeatherBuffers);
}
