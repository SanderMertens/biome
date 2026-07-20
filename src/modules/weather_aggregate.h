#include <float.h>

static
void biomeWeatherComputeAggregate(
    const WeatherGroundTile *ground,
    int32_t ground_count,
    const WeatherWaterTile *water,
    int32_t water_count,
    const WeatherAirTile *air,
    int32_t air_count,
    WeatherAggregate *result)
{
    ecs_os_zeromem(result);

    if (ground && ground_count > 0) {
        result->ground.min = (WeatherGroundTile){ FLT_MAX, FLT_MAX };
        result->ground.max = (WeatherGroundTile){ -FLT_MAX, -FLT_MAX };
        double temperature = 0;
        double moisture_amount = 0;
        for (int32_t i = 0; i < ground_count; i ++) {
            const WeatherGroundTile *tile = &ground[i];
            if (tile->temperature < result->ground.min.temperature) {
                result->ground.min.temperature = tile->temperature;
            }
            if (tile->temperature > result->ground.max.temperature) {
                result->ground.max.temperature = tile->temperature;
            }
            if (tile->moisture_amount < result->ground.min.moisture_amount) {
                result->ground.min.moisture_amount = tile->moisture_amount;
            }
            if (tile->moisture_amount > result->ground.max.moisture_amount) {
                result->ground.max.moisture_amount = tile->moisture_amount;
            }
            temperature += tile->temperature;
            moisture_amount += tile->moisture_amount;
        }
        result->ground.avg.temperature =
            (float)(temperature / ground_count);
        result->ground.avg.moisture_amount =
            (float)(moisture_amount / ground_count);
    }

    if (water && water_count > 0) {
        result->water.min = (WeatherWaterTile){ FLT_MAX, FLT_MAX };
        result->water.max = (WeatherWaterTile){ -FLT_MAX, -FLT_MAX };
        double temperature = 0;
        double water_amount = 0;
        for (int32_t i = 0; i < water_count; i ++) {
            const WeatherWaterTile *tile = &water[i];
            if (tile->temperature < result->water.min.temperature) {
                result->water.min.temperature = tile->temperature;
            }
            if (tile->temperature > result->water.max.temperature) {
                result->water.max.temperature = tile->temperature;
            }
            if (tile->water_amount < result->water.min.water_amount) {
                result->water.min.water_amount = tile->water_amount;
            }
            if (tile->water_amount > result->water.max.water_amount) {
                result->water.max.water_amount = tile->water_amount;
            }
            temperature += tile->temperature;
            water_amount += tile->water_amount;
        }
        result->water.avg.temperature =
            (float)(temperature / water_count);
        result->water.avg.water_amount =
            (float)(water_amount / water_count);
    }

    if (air && air_count > 0) {
        float min_values[8] = {
            FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX,
            FLT_MAX, FLT_MAX, FLT_MAX
        };
        float max_values[8] = {
            -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX,
            -FLT_MAX, -FLT_MAX, -FLT_MAX
        };
        double sums[8] = {0};
        for (int32_t i = 0; i < air_count; i ++) {
            const WeatherAirTile *tile = &air[i];
            float values[8] = {
                tile->temperature,
                tile->ghg_amount,
                tile->o2_amount,
                tile->vapor_amount,
                tile->water,
                tile->wind_velocity.x,
                tile->wind_velocity.y,
                tile->wind_velocity.z
            };
            for (int32_t v = 0; v < 8; v ++) {
                if (values[v] < min_values[v]) {
                    min_values[v] = values[v];
                }
                if (values[v] > max_values[v]) {
                    max_values[v] = values[v];
                }
                sums[v] += values[v];
            }
        }
        result->air.min = (WeatherAirTile){
            min_values[0], min_values[1], min_values[2], min_values[3],
            min_values[4],
            { min_values[5], min_values[6], min_values[7] }
        };
        result->air.max = (WeatherAirTile){
            max_values[0], max_values[1], max_values[2], max_values[3],
            max_values[4],
            { max_values[5], max_values[6], max_values[7] }
        };
        float averages[8];
        for (int32_t v = 0; v < 8; v ++) {
            averages[v] = (float)(sums[v] / air_count);
        }
        result->air.avg = (WeatherAirTile){
            averages[0], averages[1], averages[2], averages[3],
            averages[4],
            { averages[5], averages[6], averages[7] }
        };
    }
}
