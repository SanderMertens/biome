#include <math.h>

#define BiomeEvaporationMixingHeight (1000.0f)
#define BiomeEvaporationLatentHeat (2450000.0f)
#define BiomeEvaporationWaterHeatCapacity (4184.0f)
#define BiomeEvaporationAirDensity (1.225f)
#define BiomeEvaporationAirHeatCapacity (1005.0f)
#define BiomeEvaporationGroundHeatCapacity (2000000.0f)
#define BiomeEvaporationSurfaceEnergyShare (0.8f)

static
float biomeSaturationVaporDensity(
    float temperature)
{
    float vapor_pressure = 610.94f * expf(
        17.625f * temperature / (temperature + 243.04f));
    return vapor_pressure * 0.00216679f / (temperature + 273.15f);
}

static
float biomeSaturationVaporCapacity(
    float temperature,
    float area)
{
    return biomeSaturationVaporDensity(temperature) * area *
        BiomeEvaporationMixingHeight;
}

static
float biomeEvaporationHumidityFactor(
    float vapor_amount,
    float air_temperature,
    float area)
{
    float capacity = biomeSaturationVaporCapacity(air_temperature, area);
    if (capacity <= 0) {
        return 0;
    }
    float humidity = vapor_amount / capacity;
    if (humidity >= 1.0f) {
        return 0;
    }
    if (humidity <= 0) {
        return 1.0f;
    }
    return 1.0f - humidity;
}

static
float biomeEvaporationTemperatureFactor(
    float temperature)
{
    if (temperature <= 0) {
        return 0;
    }
    float factor = biomeSaturationVaporDensity(temperature) /
        biomeSaturationVaporDensity(20.0f);
    if (factor > 3.0f) {
        return 3.0f;
    }
    return factor;
}

static
float biomeEvaporationEnergyLimit(
    float temperature,
    float heat_capacity,
    float cooling)
{
    if (cooling <= 0) {
        return INFINITY;
    }
    if (temperature <= 0 || heat_capacity <= 0) {
        return 0;
    }
    return temperature * heat_capacity /
        (BiomeEvaporationLatentHeat * cooling);
}

static
float biomeEvaporativeCooling(
    float evaporated,
    float heat_capacity,
    float cooling)
{
    if (evaporated <= 0 || heat_capacity <= 0 || cooling <= 0) {
        return 0;
    }
    return evaporated * BiomeEvaporationLatentHeat * cooling /
        heat_capacity;
}
