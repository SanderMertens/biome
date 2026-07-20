#ifndef BIOME_WEATHER_WIND_H
#define BIOME_WEATHER_WIND_H

static float biomeAirTileArea(
    int32_t x,
    int32_t z,
    int32_t terrain_width,
    int32_t terrain_depth,
    int32_t air_scale,
    float cell_area)
{
    int32_t width = terrain_width - x * air_scale;
    int32_t depth = terrain_depth - z * air_scale;
    if (width > air_scale) {
        width = air_scale;
    }
    if (depth > air_scale) {
        depth = air_scale;
    }
    if (width <= 0 || depth <= 0) {
        return 0;
    }
    return cell_area * (float)(width * depth);
}

static void biomeComputeAirPressure(
    WeatherAirTile *air,
    int32_t width,
    int32_t depth,
    int32_t terrain_width,
    int32_t terrain_depth,
    int32_t air_scale,
    float cell_area,
    float gravity)
{
    for (int32_t z = 0; z < depth; z ++) {
        for (int32_t x = 0; x < width; x ++) {
            int32_t i = z * width + x;
            float area = biomeAirTileArea(
                x, z, terrain_width, terrain_depth, air_scale, cell_area);
            air[i].pressure = biomeAtmosphericPressure(
                &air[i], area, gravity);
        }
    }
}

static void biomeComputeWind(
    WeatherAirTile *air,
    int32_t width,
    int32_t depth,
    float gravity,
    float acceleration,
    float drag,
    float max_velocity,
    float delta_time)
{
    float damping = expf(-drag * delta_time);
    for (int32_t z = 0; z < depth; z ++) {
        for (int32_t x = 0; x < width; x ++) {
            int32_t i = z * width + x;
            float mass = biomeAtmosphericMass(&air[i]);
            if (mass <= 0) {
                air[i].wind_velocity = (flecs_vec3_t){0};
                continue;
            }

            float left = air[
                z * width + (x + width - 1) % width].pressure;
            float right = air[
                z * width + (x + 1) % width].pressure;
            float top = air[
                ((z + depth - 1) % depth) * width + x].pressure;
            float bottom = air[
                ((z + 1) % depth) * width + x].pressure;
            float surface_area = mass * gravity /
                (air[i].pressure > 0 ? air[i].pressure : 1.0f);
            float force_x = (left - right) * surface_area * 0.5f;
            float force_z = (top - bottom) * surface_area * 0.5f;

            air[i].wind_velocity.x = (
                air[i].wind_velocity.x +
                force_x / mass * acceleration * delta_time) * damping;
            air[i].wind_velocity.y = 0;
            air[i].wind_velocity.z = (
                air[i].wind_velocity.z +
                force_z / mass * acceleration * delta_time) * damping;

            float velocity = sqrtf(
                air[i].wind_velocity.x * air[i].wind_velocity.x +
                air[i].wind_velocity.z * air[i].wind_velocity.z);
            if (max_velocity > 0 && velocity > max_velocity) {
                float scale = max_velocity / velocity;
                air[i].wind_velocity.x *= scale;
                air[i].wind_velocity.z *= scale;
            }
        }
    }
}

static void biomeTransportAirFraction(
    const WeatherAirTile *source,
    WeatherAirTile *destination,
    float fraction)
{
    destination->ghg_amount += source->ghg_amount * fraction;
    destination->o2_amount += source->o2_amount * fraction;
    destination->vapor_amount += source->vapor_amount * fraction;
    destination->water += source->water * fraction;
    destination->temperature += source->temperature *
        biomeAtmosphericMass(source) * fraction;
}

static void biomeApplyWind(
    const WeatherAirTile *air,
    WeatherAirTile *next,
    int32_t width,
    int32_t depth,
    float tile_size,
    float delta_time)
{
    ecs_os_memcpy_n(next, air, WeatherAirTile, width * depth);
    for (int32_t i = 0; i < width * depth; i ++) {
        next[i].temperature = 0;
        next[i].ghg_amount = 0;
        next[i].o2_amount = 0;
        next[i].vapor_amount = 0;
        next[i].water = 0;
    }

    for (int32_t z = 0; z < depth; z ++) {
        for (int32_t x = 0; x < width; x ++) {
            int32_t i = z * width + x;
            float fx = fabsf(air[i].wind_velocity.x) *
                delta_time / tile_size;
            float fz = fabsf(air[i].wind_velocity.z) *
                delta_time / tile_size;
            if (fx > 1.0f) {
                fx = 1.0f;
            }
            if (fz > 1.0f) {
                fz = 1.0f;
            }

            int32_t nx = (x +
                (air[i].wind_velocity.x < 0 ? width - 1 : 1)) % width;
            int32_t nz = (z +
                (air[i].wind_velocity.z < 0 ? depth - 1 : 1)) % depth;

            biomeTransportAirFraction(
                &air[i], &next[i], (1.0f - fx) * (1.0f - fz));
            if (fx > 0) {
                biomeTransportAirFraction(
                    &air[i], &next[z * width + nx], fx * (1.0f - fz));
            }
            if (fz > 0) {
                biomeTransportAirFraction(
                    &air[i], &next[nz * width + x], (1.0f - fx) * fz);
            }
            if (fx > 0 && fz > 0) {
                biomeTransportAirFraction(
                    &air[i], &next[nz * width + nx], fx * fz);
            }
        }
    }

    for (int32_t i = 0; i < width * depth; i ++) {
        float mass = biomeAtmosphericMass(&next[i]);
        next[i].temperature = mass > 0
            ? next[i].temperature / mass
            : air[i].temperature;
    }
}

static void biomeTransportAirFace(
    const WeatherAirTile *air,
    WeatherAirTile *next,
    int32_t first,
    int32_t second,
    float velocity,
    float time_scale)
{
    int32_t source = velocity >= 0 ? first : second;
    int32_t destination = velocity >= 0 ? second : first;
    float fraction = fabsf(velocity) * time_scale;
    if (fraction > 0.25f) {
        fraction = 0.25f;
    }
    float ghg = air[source].ghg_amount * fraction;
    float o2 = air[source].o2_amount * fraction;
    float vapor = air[source].vapor_amount * fraction;
    float water = air[source].water * fraction;
    float energy = air[source].temperature *
        biomeAtmosphericMass(&air[source]) * fraction;
    next[source].ghg_amount -= ghg;
    next[source].o2_amount -= o2;
    next[source].vapor_amount -= vapor;
    next[source].water -= water;
    next[source].temperature -= energy;
    next[destination].ghg_amount += ghg;
    next[destination].o2_amount += o2;
    next[destination].vapor_amount += vapor;
    next[destination].water += water;
    next[destination].temperature += energy;
}

static void biomeApplyWindConservative(
    const WeatherAirTile *air,
    WeatherAirTile *next,
    int32_t width,
    int32_t depth,
    float tile_size,
    float delta_time)
{
    ecs_os_memcpy_n(next, air, WeatherAirTile, width * depth);
    for (int32_t i = 0; i < width * depth; i ++) {
        next[i].temperature = air[i].temperature *
            biomeAtmosphericMass(&air[i]);
    }

    float time_scale = delta_time / tile_size;
    for (int32_t z = 0; z < depth; z ++) {
        for (int32_t x = 0; x < width; x ++) {
            int32_t i = z * width + x;
            int32_t right = z * width + (x + 1) % width;
            int32_t bottom = ((z + 1) % depth) * width + x;
            float velocity_x = 0.5f * (
                air[i].wind_velocity.x +
                air[right].wind_velocity.x);
            float velocity_z = 0.5f * (
                air[i].wind_velocity.z +
                air[bottom].wind_velocity.z);
            biomeTransportAirFace(
                air, next, i, right, velocity_x, time_scale);
            biomeTransportAirFace(
                air, next, i, bottom, velocity_z, time_scale);
        }
    }

    for (int32_t i = 0; i < width * depth; i ++) {
        float mass = biomeAtmosphericMass(&next[i]);
        next[i].temperature = mass > 0
            ? next[i].temperature / mass
            : air[i].temperature;
    }
}

#endif
