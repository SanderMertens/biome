#ifndef BIOME_RADIATIVE_BALANCE_H
#define BIOME_RADIATIVE_BALANCE_H

#include <math.h>

static
float biomeRadiativeTemperatureChange(
    float temperature,
    float ghg_amount,
    float vapor_amount,
    float stellar_intensity,
    const WeatherRadiativeBalance *config,
    float delta_time)
{
    float kelvin = temperature + 273.15f;
    if (kelvin < 0) {
        kelvin = 0;
    }

    float absorbers = ghg_amount + vapor_amount;
    if (absorbers < 0) {
        absorbers = 0;
    }

    float optical_depth = config->greenhouse_effect * absorbers;
    if (optical_depth < 0) {
        optical_depth = 0;
    }

    float outgoing_fraction = expf(-optical_depth);
    float relative_temperature = kelvin / 288.15f;
    float cooling = config->radiative_cooling *
        relative_temperature * relative_temperature *
        relative_temperature * relative_temperature *
        outgoing_fraction;
    float heating = config->stellar_heating *
        (stellar_intensity > 0 ? stellar_intensity : 0);

    return (heating - cooling) * delta_time;
}

static
void biomeRadiativeBalanceSurface(
    WeatherGroundTile *ground,
    WeatherWaterTile *water,
    WeatherAirTile *air,
    int32_t ground_width,
    int32_t ground_depth,
    int32_t water_width,
    int32_t water_depth,
    int32_t ground_scale,
    float stellar_intensity,
    const WeatherRadiativeBalance *config,
    float delta_time)
{
    if (ground_scale <= 0) {
        ground_scale = 1;
    }

    for (int32_t z = 0; z < ground_depth; z ++) {
        for (int32_t x = 0; x < ground_width; x ++) {
            int32_t i = z * ground_width + x;
            bool exposed_ground = water == NULL;
            int32_t min_x = x * ground_scale;
            int32_t min_z = z * ground_scale;
            int32_t max_x = min_x + ground_scale;
            int32_t max_z = min_z + ground_scale;
            if (max_x > water_width) {
                max_x = water_width;
            }
            if (max_z > water_depth) {
                max_z = water_depth;
            }

            if (water) {
                for (int32_t water_z = min_z; water_z < max_z; water_z ++) {
                    for (int32_t water_x = min_x; water_x < max_x; water_x ++) {
                        WeatherWaterTile *water_tile = &water[
                            water_z * water_width + water_x];
                        if (water_tile->water_amount <= 0) {
                            exposed_ground = true;
                            continue;
                        }
                        water_tile->temperature +=
                            biomeRadiativeTemperatureChange(
                                water_tile->temperature,
                                air[i].ghg_amount,
                                air[i].vapor_amount,
                                stellar_intensity,
                                config,
                                delta_time);
                    }
                }
            }

            if (exposed_ground) {
                ground[i].temperature += biomeRadiativeTemperatureChange(
                    ground[i].temperature,
                    air[i].ghg_amount,
                    air[i].vapor_amount,
                    stellar_intensity,
                    config,
                    delta_time);
            }

            air[i].temperature += biomeRadiativeTemperatureChange(
                air[i].temperature,
                air[i].ghg_amount,
                air[i].vapor_amount,
                0,
                config,
                delta_time);
        }
    }
}

#endif
