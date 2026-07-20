#include <math.h>

#define BiomeEvaporationStandardPressure (101325.0f)
#define BiomeEvaporationLatentHeat (2450000.0f)
#define BiomeEvaporationWaterHeatCapacity (4184.0f)
#define BiomeEvaporationAirHeatCapacity (1005.0f)
#define BiomeEvaporationGroundHeatCapacity (2000000.0f)
#define BiomeEvaporationMaxAirEnergyShare (0.2f)

static
float biomeSaturationVaporPressure(
    float temperature)
{
    return 610.94f * expf(
        17.625f * temperature / (temperature + 243.04f));
}

static
float biomeSaturationVaporDensity(
    float temperature)
{
    return biomeSaturationVaporPressure(temperature) *
        0.00216679f / (temperature + 273.15f);
}

static
float biomeAtmosphericMass(
    const WeatherAirTile *air)
{
    float mass = air->ghg_amount + air->o2_amount + air->vapor_amount;
    return mass > 0 ? mass : 0;
}

static
float biomeAtmosphericPressure(
    const WeatherAirTile *air,
    float area,
    float gravity)
{
    if (area <= 0 || gravity <= 0) {
        return 0;
    }
    return biomeAtmosphericMass(air) * gravity / area;
}

static
float biomeSaturationVaporCapacity(
    float temperature,
    float area,
    float gravity)
{
    if (area <= 0 || gravity <= 0) {
        return 0;
    }
    return biomeSaturationVaporPressure(temperature) * area /
        gravity;
}

static
float biomeEvaporationAirEnergyShare(
    float pressure)
{
    if (pressure <= 0) {
        return 0;
    }
    float pressure_factor = pressure / BiomeEvaporationStandardPressure;
    if (pressure_factor > 1.0f) {
        pressure_factor = 1.0f;
    }
    return BiomeEvaporationMaxAirEnergyShare * pressure_factor;
}

static
float biomeEvaporationHumidityFactor(
    float vapor_amount,
    float air_temperature,
    float area,
    float gravity)
{
    float capacity = biomeSaturationVaporCapacity(
        air_temperature, area, gravity);
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
