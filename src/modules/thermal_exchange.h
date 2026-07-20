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

#endif
