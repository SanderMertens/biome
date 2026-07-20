static float biomeWeatherOceanWaterAmount(
    float seed_water,
    float terrain_height,
    float ocean_level,
    float density)
{
    if (terrain_height >= ocean_level) {
        return seed_water;
    }

    float ocean_water =
        (ocean_level - terrain_height) * density *
        (float)(TerrainCellSize * TerrainCellSize);
    return seed_water > ocean_water ? seed_water : ocean_water;
}
