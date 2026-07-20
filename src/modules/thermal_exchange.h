#ifndef BIOME_THERMAL_EXCHANGE_H
#define BIOME_THERMAL_EXCHANGE_H

static float biomeThermalExchangeFactor(
    float rate,
    float delta_time)
{
    float factor = 1.0f - expf(-rate * delta_time);
    return factor < 0.25f ? factor : 0.25f;
}

static void biomeThermalTransfer(
    float temperature_a,
    float temperature_b,
    float *next_a,
    float *next_b,
    float factor)
{
    float transfer = (temperature_b - temperature_a) * factor;
    *next_a += transfer;
    *next_b -= transfer;
}

static void biomeWaterThermalTransfer(
    const WeatherWaterTile *water_a,
    const WeatherWaterTile *water_b,
    WeatherWaterTile *next_a,
    WeatherWaterTile *next_b,
    float factor)
{
    float total_amount = water_a->water_amount + water_b->water_amount;
    float transfer = (water_b->temperature - water_a->temperature) * factor *
        (2.0f * water_a->water_amount * water_b->water_amount / total_amount);
    next_a->temperature += transfer / water_a->water_amount;
    next_b->temperature -= transfer / water_b->water_amount;
}

static void biomeGroundThermalExchange(
    const WeatherGroundTile *ground,
    WeatherGroundTile *next,
    int32_t width,
    int32_t depth,
    float factor)
{
    ecs_os_memcpy_n(next, ground, WeatherGroundTile, width * depth);
    for (int32_t z = 0; z < depth; z ++) {
        for (int32_t x = 0; x < width; x ++) {
            int32_t i = z * width + x;
            if (x + 1 < width) {
                int32_t neighbor = i + 1;
                biomeThermalTransfer(
                    ground[i].temperature, ground[neighbor].temperature,
                    &next[i].temperature, &next[neighbor].temperature, factor);
            }
            if (z + 1 < depth) {
                int32_t neighbor = i + width;
                biomeThermalTransfer(
                    ground[i].temperature, ground[neighbor].temperature,
                    &next[i].temperature, &next[neighbor].temperature, factor);
            }
        }
    }
}

static void biomeWaterThermalExchange(
    const WeatherWaterTile *water,
    WeatherWaterTile *next,
    int32_t width,
    int32_t depth,
    float factor)
{
    ecs_os_memcpy_n(next, water, WeatherWaterTile, width * depth);
    for (int32_t z = 0; z < depth; z ++) {
        for (int32_t x = 0; x < width; x ++) {
            int32_t i = z * width + x;
            if (water[i].water_amount <= 0) {
                continue;
            }
            if (x + 1 < width) {
                int32_t neighbor = i + 1;
                if (water[neighbor].water_amount > 0) {
                    biomeWaterThermalTransfer(
                        &water[i], &water[neighbor],
                        &next[i], &next[neighbor],
                        factor);
                }
            }
            if (z + 1 < depth) {
                int32_t neighbor = i + width;
                if (water[neighbor].water_amount > 0) {
                    biomeWaterThermalTransfer(
                        &water[i], &water[neighbor],
                        &next[i], &next[neighbor],
                        factor);
                }
            }
        }
    }
}

static void biomeAirThermalExchange(
    const WeatherAirTile *air,
    WeatherAirTile *next,
    int32_t width,
    int32_t depth,
    float factor)
{
    ecs_os_memcpy_n(next, air, WeatherAirTile, width * depth);
    for (int32_t z = 0; z < depth; z ++) {
        for (int32_t x = 0; x < width; x ++) {
            int32_t i = z * width + x;
            if (x + 1 < width) {
                int32_t neighbor = i + 1;
                biomeThermalTransfer(
                    air[i].temperature, air[neighbor].temperature,
                    &next[i].temperature, &next[neighbor].temperature, factor);
            }
            if (z + 1 < depth) {
                int32_t neighbor = i + width;
                biomeThermalTransfer(
                    air[i].temperature, air[neighbor].temperature,
                    &next[i].temperature, &next[neighbor].temperature, factor);
            }
        }
    }
}

static void biomeGroundThermalExchangePeriodic(
    const WeatherGroundTile *ground,
    WeatherGroundTile *next,
    int32_t width,
    int32_t depth,
    float factor)
{
    biomeGroundThermalExchange(ground, next, width, depth, factor);
    if (width > 2) {
        for (int32_t z = 0; z < depth; z ++) {
            int32_t first = z * width;
            int32_t last = first + width - 1;
            biomeThermalTransfer(
                ground[last].temperature, ground[first].temperature,
                &next[last].temperature, &next[first].temperature, factor);
        }
    }
    if (depth > 2) {
        for (int32_t x = 0; x < width; x ++) {
            int32_t first = x;
            int32_t last = (depth - 1) * width + x;
            biomeThermalTransfer(
                ground[last].temperature, ground[first].temperature,
                &next[last].temperature, &next[first].temperature, factor);
        }
    }
}

static void biomeWaterThermalExchangePeriodic(
    const WeatherWaterTile *water,
    WeatherWaterTile *next,
    int32_t width,
    int32_t depth,
    float factor)
{
    biomeWaterThermalExchange(water, next, width, depth, factor);
    if (width > 2) {
        for (int32_t z = 0; z < depth; z ++) {
            int32_t first = z * width;
            int32_t last = first + width - 1;
            if (water[first].water_amount > 0 &&
                water[last].water_amount > 0)
            {
                biomeWaterThermalTransfer(
                    &water[last], &water[first],
                    &next[last], &next[first], factor);
            }
        }
    }
    if (depth > 2) {
        for (int32_t x = 0; x < width; x ++) {
            int32_t first = x;
            int32_t last = (depth - 1) * width + x;
            if (water[first].water_amount > 0 &&
                water[last].water_amount > 0)
            {
                biomeWaterThermalTransfer(
                    &water[last], &water[first],
                    &next[last], &next[first], factor);
            }
        }
    }
}

static void biomeAirThermalExchangePeriodic(
    const WeatherAirTile *air,
    WeatherAirTile *next,
    int32_t width,
    int32_t depth,
    float factor)
{
    biomeAirThermalExchange(air, next, width, depth, factor);
    if (width > 2) {
        for (int32_t z = 0; z < depth; z ++) {
            int32_t first = z * width;
            int32_t last = first + width - 1;
            biomeThermalTransfer(
                air[last].temperature, air[first].temperature,
                &next[last].temperature, &next[first].temperature, factor);
        }
    }
    if (depth > 2) {
        for (int32_t x = 0; x < width; x ++) {
            int32_t first = x;
            int32_t last = (depth - 1) * width + x;
            biomeThermalTransfer(
                air[last].temperature, air[first].temperature,
                &next[last].temperature, &next[first].temperature, factor);
        }
    }
}

static void biomeSurfaceThermalExchange(
    const WeatherGroundTile *ground,
    const WeatherWaterTile *water,
    const WeatherAirTile *air,
    WeatherGroundTile *next_ground,
    WeatherWaterTile *next_water,
    WeatherAirTile *next_air,
    int32_t ground_width,
    int32_t ground_depth,
    int32_t water_width,
    int32_t water_depth,
    int32_t scale,
    float factor)
{
    for (int32_t z = 0; z < ground_depth; z ++) {
        int32_t min_z = z * scale;
        int32_t max_z = min_z + scale;
        if (max_z > water_depth) {
            max_z = water_depth;
        }
        for (int32_t x = 0; x < ground_width; x ++) {
            int32_t min_x = x * scale;
            int32_t max_x = min_x + scale;
            if (max_x > water_width) {
                max_x = water_width;
            }

            int32_t surface_count = (max_x - min_x) * (max_z - min_z);
            if (surface_count <= 0) {
                continue;
            }

            int32_t i = z * ground_width + x;
            float surface_factor = factor / (float)surface_count;
            for (int32_t wz = min_z; wz < max_z; wz ++) {
                for (int32_t wx = min_x; wx < max_x; wx ++) {
                    int32_t wi = wz * water_width + wx;
                    if (water[wi].water_amount > 0) {
                        float transfer = (
                            water[wi].temperature - air[i].temperature) *
                            factor;
                        next_air[i].temperature +=
                            transfer / (float)surface_count;
                        next_water[wi].temperature -= transfer;
                    } else {
                        float transfer = (
                            ground[i].temperature - air[i].temperature) *
                            surface_factor;
                        next_air[i].temperature += transfer;
                        next_ground[i].temperature -= transfer;
                    }
                }
            }
        }
    }
}

#endif
