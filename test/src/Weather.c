#include <biome.h>
#include "../../src/modules/evaporation.h"
#include "../../src/modules/radiative_balance.h"
#include "../../src/modules/thermal_exchange.h"
#include "../../src/modules/weather_aggregate.h"
#include "../../src/modules/weather_ocean.h"
#include <biome_test.h>

void Weather_thermal_exchange(void) {
    float slow_factor = biomeThermalExchangeFactor(0.05f, 1.0f);
    float fast_factor = biomeThermalExchangeFactor(0.1f, 1.0f);
    test_assert(fast_factor > slow_factor);

    WeatherGroundTile ground[3] = {
        { .temperature = 100.0f },
        { .temperature = 0.0f },
        { .temperature = 0.0f }
    };
    WeatherGroundTile next_ground[3];
    biomeGroundThermalExchange(ground, next_ground, 3, 1, 0.25f);

    test_flt(next_ground[0].temperature, 75.0f);
    test_flt(next_ground[1].temperature, 25.0f);
    test_flt(next_ground[2].temperature, 0.0f);
    test_flt(
        next_ground[0].temperature +
        next_ground[1].temperature +
        next_ground[2].temperature,
        100.0f);

    WeatherWaterTile water[3] = {
        { .temperature = 100.0f, .water_amount = 1.0f },
        { .temperature = -20.0f, .water_amount = 0.0f },
        { .temperature = 0.0f, .water_amount = 1.0f }
    };
    WeatherWaterTile next_water[3];
    biomeWaterThermalExchange(water, next_water, 3, 1, 0.25f);

    test_flt(next_water[0].temperature, 100.0f);
    test_flt(next_water[1].temperature, -20.0f);
    test_flt(next_water[2].temperature, 0.0f);

    WeatherWaterTile unequal_water[2] = {
        { .temperature = 100.0f, .water_amount = 1.0f },
        { .temperature = 0.0f, .water_amount = 3.0f }
    };
    WeatherWaterTile next_unequal_water[2];
    biomeWaterThermalExchange(
        unequal_water, next_unequal_water, 2, 1, 0.25f);

    test_flt(next_unequal_water[0].temperature, 62.5f);
    test_flt(next_unequal_water[1].temperature, 12.5f);
    test_flt(
        next_unequal_water[0].temperature *
            next_unequal_water[0].water_amount +
        next_unequal_water[1].temperature *
            next_unequal_water[1].water_amount,
        100.0f);

    WeatherAirTile air[2] = {
        { .temperature = -10.0f },
        { .temperature = 30.0f }
    };
    WeatherAirTile next_air[2];
    biomeAirThermalExchange(air, next_air, 2, 1, 0.25f);

    test_flt(next_air[0].temperature, 0.0f);
    test_flt(next_air[1].temperature, 20.0f);
}

void Weather_evaporation(void) {
    float area = 900.0f;
    float gravity = 9.80665f;
    float saturation = biomeSaturationVaporPressure(20.0f) * area /
        gravity;

    test_assert(saturation > 214000.0f);
    test_assert(saturation < 215000.0f);
    test_flt(
        biomeSaturationVaporCapacity(20.0f, area, gravity),
        saturation);
    test_flt(
        biomeSaturationVaporCapacity(20.0f, area, gravity * 0.5f),
        saturation * 2.0f);
    test_flt(
        biomeEvaporationHumidityFactor(0, 20.0f, area, gravity),
        1.0f);
    test_flt(
        biomeEvaporationHumidityFactor(
            saturation * 0.5f, 20.0f, area, gravity),
        0.5f);
    test_flt(
        biomeEvaporationHumidityFactor(
            saturation, 20.0f, area, gravity),
        0.0f);
    WeatherAirTile vacuum = {0};
    WeatherAirTile atmosphere = {
        .ghg_amount = 1000.0f,
        .o2_amount = 2000.0f,
        .vapor_amount = 500.0f
    };
    test_flt(
        biomeAtmosphericPressure(&vacuum, area, gravity),
        0.0f);
    test_flt(
        biomeAtmosphericPressure(&atmosphere, area, gravity),
        3500.0f * gravity / area);
    test_flt(
        biomeAtmosphericPressure(&atmosphere, area, gravity * 0.5f),
        3500.0f * gravity * 0.5f / area);
    test_flt(biomeEvaporationAirEnergyShare(0), 0.0f);
    test_flt(
        biomeEvaporationAirEnergyShare(
            BiomeEvaporationStandardPressure),
        BiomeEvaporationMaxAirEnergyShare);
    test_assert(
        biomeEvaporationTemperatureFactor(30.0f) >
        biomeEvaporationTemperatureFactor(20.0f));
    test_flt(biomeEvaporationTemperatureFactor(0.0f), 0.0f);
    test_assert(biomeEvaporativeCooling(1.0f, 4184.0f, 1.0f) > 500.0f);
    test_flt(biomeEvaporativeCooling(1.0f, 4184.0f, 0.0f), 0.0f);
    test_flt(
        biomeEvaporationEnergyLimit(20.0f, 4184.0f, 1.0f),
        20.0f * 4184.0f / BiomeEvaporationLatentHeat);
    test_assert(isinf(
        biomeEvaporationEnergyLimit(20.0f, 4184.0f, 0.0f)));
}

void Weather_aggregate(void) {
    WeatherGroundTile ground[2] = {
        { .temperature = 10.0f, .moisture_amount = 20.0f },
        { .temperature = 30.0f, .moisture_amount = 60.0f }
    };
    WeatherWaterTile water[2] = {
        { .temperature = 5.0f, .water_amount = 100.0f },
        { .temperature = 15.0f, .water_amount = 300.0f }
    };
    WeatherAirTile air[2] = {
        {
            .temperature = -10.0f,
            .ghg_amount = 2.0f,
            .o2_amount = 4.0f,
            .vapor_amount = 6.0f,
            .water = 8.0f,
            .wind_velocity = { -3.0f, 2.0f, 7.0f }
        },
        {
            .temperature = 30.0f,
            .ghg_amount = 6.0f,
            .o2_amount = 8.0f,
            .vapor_amount = 10.0f,
            .water = 12.0f,
            .wind_velocity = { 5.0f, -4.0f, 1.0f }
        }
    };
    WeatherAggregate aggregate;

    biomeWeatherComputeAggregate(
        ground, 2, water, 2, air, 2, &aggregate);

    test_flt(aggregate.ground.min.temperature, 10.0f);
    test_flt(aggregate.ground.avg.temperature, 20.0f);
    test_flt(aggregate.ground.max.temperature, 30.0f);
    test_flt(aggregate.ground.min.moisture_amount, 20.0f);
    test_flt(aggregate.ground.avg.moisture_amount, 40.0f);
    test_flt(aggregate.ground.max.moisture_amount, 60.0f);
    test_flt(aggregate.water.min.temperature, 5.0f);
    test_flt(aggregate.water.avg.temperature, 10.0f);
    test_flt(aggregate.water.max.temperature, 15.0f);
    test_flt(aggregate.water.min.water_amount, 100.0f);
    test_flt(aggregate.water.avg.water_amount, 200.0f);
    test_flt(aggregate.water.max.water_amount, 300.0f);
    test_flt(aggregate.air.min.temperature, -10.0f);
    test_flt(aggregate.air.avg.temperature, 10.0f);
    test_flt(aggregate.air.max.temperature, 30.0f);
    test_flt(aggregate.air.min.vapor_amount, 6.0f);
    test_flt(aggregate.air.avg.vapor_amount, 8.0f);
    test_flt(aggregate.air.max.vapor_amount, 10.0f);
    test_flt(aggregate.air.min.wind_velocity.x, -3.0f);
    test_flt(aggregate.air.avg.wind_velocity.x, 1.0f);
    test_flt(aggregate.air.max.wind_velocity.x, 5.0f);
    test_flt(aggregate.air.min.wind_velocity.y, -4.0f);
    test_flt(aggregate.air.avg.wind_velocity.y, -1.0f);
    test_flt(aggregate.air.max.wind_velocity.y, 2.0f);
    test_flt(aggregate.air.min.wind_velocity.z, 1.0f);
    test_flt(aggregate.air.avg.wind_velocity.z, 4.0f);
    test_flt(aggregate.air.max.wind_velocity.z, 7.0f);

    WeatherFloatAggregate pressure;
    biomeWeatherComputePressureAggregate(
        air, 2, 1, 10, 6, 6, 25.0f, 9.80665f, &pressure);
    test_flt(
        pressure.min,
        12.0f * 9.80665f / 900.0f);
    test_flt(
        pressure.avg,
        (12.0f * 9.80665f / 900.0f +
         24.0f * 9.80665f / 600.0f) * 0.5f);
    test_flt(
        pressure.max,
        24.0f * 9.80665f / 600.0f);
}

void Weather_radiative_balance(void) {
    WeatherRadiativeBalance config = {
        .radiative_cooling = 2.0f,
        .stellar_heating = 1.0f,
        .greenhouse_effect = 0.5f
    };

    float no_atmosphere = biomeRadiativeTemperatureChange(
        15.0f, 0.0f, 0.0f, 1.0f, &config, 1.0f);
    float ghg = biomeRadiativeTemperatureChange(
        15.0f, 2.0f, 0.0f, 1.0f, &config, 1.0f);
    float vapor = biomeRadiativeTemperatureChange(
        15.0f, 0.0f, 2.0f, 1.0f, &config, 1.0f);
    float dense_atmosphere = biomeRadiativeTemperatureChange(
        15.0f, 5.0f, 5.0f, 1.0f, &config, 1.0f);

    test_assert(no_atmosphere < 0);
    test_assert(ghg > no_atmosphere);
    test_flt(vapor, ghg);
    test_assert(dense_atmosphere > ghg);
    test_assert(dense_atmosphere < config.stellar_heating);

    float cold = biomeRadiativeTemperatureChange(
        -50.0f, 0.0f, 0.0f, 0.0f, &config, 1.0f);
    float hot = biomeRadiativeTemperatureChange(
        50.0f, 0.0f, 0.0f, 0.0f, &config, 1.0f);
    test_assert(hot < cold);

    float dark = biomeRadiativeTemperatureChange(
        15.0f, 0.0f, 0.0f, 0.0f, &config, 1.0f);
    float bright = biomeRadiativeTemperatureChange(
        15.0f, 0.0f, 0.0f, 2.0f, &config, 1.0f);
    test_flt(bright - dark, 2.0f);

    WeatherGroundTile ground[2] = {
        { .temperature = -20.0f },
        { .temperature = -20.0f }
    };
    WeatherWaterTile water[2] = {
        { .temperature = -20.0f, .water_amount = 100.0f },
        { .temperature = -20.0f }
    };
    WeatherAirTile air[2] = {
        { .temperature = -20.0f },
        { .temperature = -20.0f }
    };
    WeatherRadiativeBalance heating = {
        .stellar_heating = 10.0f
    };

    biomeRadiativeBalanceSurface(
        ground, water, air, 2, 1, 2, 1, 1, 1.0f, &heating, 1.0f);

    test_flt(ground[0].temperature, -20.0f);
    test_flt(water[0].temperature, -10.0f);
    test_flt(ground[1].temperature, -10.0f);
    test_flt(water[1].temperature, -20.0f);
    test_flt(air[0].temperature, -20.0f);
}

void Weather_ocean_level(void) {
    float expected = 10.0f * 997.0f *
        (float)(TerrainCellSize * TerrainCellSize);
    test_flt(
        biomeWeatherOceanWaterAmount(0.0f, -10.0f, 0.0f, 997.0f),
        expected);
    test_flt(
        biomeWeatherOceanWaterAmount(
            expected + 1.0f, -10.0f, 0.0f, 997.0f),
        expected + 1.0f);
    test_flt(
        biomeWeatherOceanWaterAmount(100.0f, 10.0f, 0.0f, 997.0f),
        100.0f);
}
