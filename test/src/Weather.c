#include <biome.h>
#include "../../src/modules/radiative_balance.h"
#include "../../src/modules/thermal_exchange.h"
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
