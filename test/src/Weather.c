#include <biome.h>
#include "../../src/modules/weather_model.h"
#include <biome_test.h>

void Weather_global_temperature(void) {
    Weather vacuum = {
        .gravity = BiomeWeatherStandardGravity,
        .radiative_cooling = 2.0f,
        .stellar_heating = 1.0f,
        .temperature = 15.0f
    };
    Weather atmosphere = vacuum;
    atmosphere.pressure = BiomeWeatherStandardPressure;
    atmosphere.greenhouse_gas = 1.0f;
    Weather low_gravity = atmosphere;
    low_gravity.gravity *= 0.5f;

    float vacuum_change = biomeWeatherTemperatureChange(
        &vacuum, 1.0f);
    float atmosphere_change = biomeWeatherTemperatureChange(
        &atmosphere, 1.0f);
    float low_gravity_change = biomeWeatherTemperatureChange(
        &low_gravity, 1.0f);

    test_assert(vacuum_change < 0);
    test_assert(atmosphere_change > vacuum_change);
    test_assert(low_gravity_change > atmosphere_change);

    Weather dark = vacuum;
    dark.stellar_heating = 0;
    test_flt(
        vacuum_change - biomeWeatherTemperatureChange(&dark, 1.0f),
        1.0f);
    test_flt(biomeWeatherTemperatureChange(&vacuum, 0), 0);
}

void Weather_water_state(void) {
    test_assert(biomeWeatherWaterIsLiquid(
        20.0f, BiomeWeatherStandardPressure));
    test_assert(!biomeWeatherWaterIsLiquid(-1.0f, 1000000.0f));
    test_assert(!biomeWeatherWaterIsLiquid(
        0.0f, BiomeWeatherWaterTriplePointPressure - 1.0f));
    test_assert(!biomeWeatherWaterIsLiquid(100.0f, 100000.0f));
    test_assert(!biomeWeatherWaterIsLiquid(400.0f, 100000000.0f));
}
