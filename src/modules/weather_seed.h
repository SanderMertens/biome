#ifndef BIOME_WEATHER_SEED_H
#define BIOME_WEATHER_SEED_H

static float biomeWeatherPeriodicFbm2(
    float x,
    float z,
    float width,
    float depth,
    float frequency,
    float offset_x,
    float offset_z,
    int32_t octaves)
{
    float u = width > 0 ? x / width : 0;
    float v = depth > 0 ? z / depth : 0;
    float px = x * frequency + offset_x;
    float pz = z * frequency + offset_z;
    float period_x = width * frequency;
    float period_z = depth * frequency;
    float n00 = biomeFbm2(px, pz, octaves);
    float n10 = biomeFbm2(px - period_x, pz, octaves);
    float n01 = biomeFbm2(px, pz - period_z, octaves);
    float n11 = biomeFbm2(
        px - period_x, pz - period_z, octaves);
    float nx0 = n00 + (n10 - n00) * u;
    float nx1 = n01 + (n11 - n01) * u;
    return nx0 + (nx1 - nx0) * v;
}

static float biomeWeatherTileCoverage(
    int32_t x,
    int32_t z,
    int32_t width,
    int32_t depth,
    int32_t scale)
{
    int32_t covered_width = width - x * scale;
    int32_t covered_depth = depth - z * scale;
    if (covered_width > scale) {
        covered_width = scale;
    }
    if (covered_depth > scale) {
        covered_depth = scale;
    }
    if (covered_width <= 0 || covered_depth <= 0 || scale <= 0) {
        return 0;
    }
    return (float)(covered_width * covered_depth) /
        (float)(scale * scale);
}

#endif
