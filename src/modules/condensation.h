#ifndef BIOME_CONDENSATION_H
#define BIOME_CONDENSATION_H

static float biomeCondensationFactor(
    float rate,
    float delta_time)
{
    return 1.0f - expf(-rate * delta_time);
}

static void biomeCondensationStep(
    WeatherAirTile *air,
    int32_t air_width,
    int32_t air_depth,
    WeatherWaterTile *water,
    int32_t water_width,
    int32_t water_depth,
    int32_t scale,
    float cell_area,
    float gravity,
    const WeatherCondensation *config,
    float delta_time)
{
    for (int32_t z = 0; z < air_depth; z ++) {
        int32_t min_z = z * scale;
        int32_t max_z = min_z + scale;
        if (max_z > water_depth) {
            max_z = water_depth;
        }
        for (int32_t x = 0; x < air_width; x ++) {
            int32_t min_x = x * scale;
            int32_t max_x = min_x + scale;
            if (max_x > water_width) {
                max_x = water_width;
            }

            WeatherAirTile *tile = &air[z * air_width + x];
            tile->precipitation_rate = 0;
            int32_t surface_count = (max_x - min_x) * (max_z - min_z);
            if (!config->enabled || surface_count <= 0 ||
                cell_area <= 0 || gravity <= 0)
            {
                continue;
            }

            float area = cell_area * (float)surface_count;
            float capacity = biomeSaturationVaporCapacity(
                tile->temperature, area, gravity);
            float excess = tile->vapor_amount - capacity;
            if (excess > 0 && config->rate > 0) {
                float condensed = excess * biomeCondensationFactor(
                    config->rate, delta_time);
                float atmospheric_mass = biomeAtmosphericMass(tile);
                tile->vapor_amount -= condensed;
                tile->water += condensed;
                if (config->latent_heating > 0 && atmospheric_mass > 0) {
                    tile->temperature += condensed *
                        BiomeEvaporationLatentHeat *
                        config->latent_heating /
                        (atmospheric_mass *
                            BiomeEvaporationAirHeatCapacity);
                }
            }

            float threshold = config->precipitation_threshold > 0
                ? config->precipitation_threshold
                : 0;
            float cloud_excess = tile->water - threshold;
            if (cloud_excess <= 0 || config->precipitation_rate <= 0) {
                continue;
            }

            float precipitated = cloud_excess * biomeCondensationFactor(
                config->precipitation_rate, delta_time);
            tile->water -= precipitated;
            tile->precipitation_rate = precipitated / delta_time;
            float amount = precipitated / (float)surface_count;
            for (int32_t wz = min_z; wz < max_z; wz ++) {
                for (int32_t wx = min_x; wx < max_x; wx ++) {
                    WeatherWaterTile *surface =
                        &water[wz * water_width + wx];
                    float total = surface->water_amount + amount;
                    surface->temperature = total > 0
                        ? (surface->temperature * surface->water_amount +
                            tile->temperature * amount) / total
                        : tile->temperature;
                    surface->water_amount = total;
                }
            }
        }
    }
}

#endif
