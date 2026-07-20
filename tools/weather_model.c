#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Width (128)
#define Depth (128)
#define CellSize (5.0)
#define CellArea (25.0)
#define Gravity (9.80665)
#define AirHeatCapacity (1005.0)
#define LatentHeat (2450000.0)
#define WindAcceleration (1.0)
#define WindDrag (0.1)
#define MaxVelocity (30.0)
#define CondensationRate (1.0)
#define PrecipitationRate (0.2)
#define PrecipitationThreshold (100.0)
#define ThermalRate (0.2)
#define DeltaTime (1.0 / 60.0)
#define Pi (3.14159265358979323846)

typedef struct Tile {
    double temperature;
    double dry;
    double vapor;
    double cloud;
    double pressure;
    double velocity_x;
    double velocity_z;
} Tile;

static int32_t indexAt(int32_t x, int32_t z) {
    return ((z + Depth) % Depth) * Width + (x + Width) % Width;
}

static double gasMass(const Tile *tile) {
    return tile->dry + tile->vapor;
}

static double pressureMass(const Tile *tile, int cloud_pressure) {
    return gasMass(tile) + (cloud_pressure ? tile->cloud : 0);
}

static double saturationPressure(double temperature) {
    return 610.94 * exp(
        17.625 * temperature / (temperature + 243.04));
}

static double saturationCapacity(double temperature) {
    return saturationPressure(temperature) * CellArea / Gravity;
}

static double exchangeFactor(double rate) {
    double factor = 1.0 - exp(-rate * DeltaTime);
    return factor < 0.25 ? factor : 0.25;
}

static double randomUnit(uint32_t *state) {
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return (double)(value & 0xffffff) / (double)0x1000000;
}

static void initialize(
    Tile *tiles,
    double dry,
    const char *scenario)
{
    uint32_t random_state = 0x9e3779b9u;
    for (int32_t z = 0; z < Depth; z ++) {
        for (int32_t x = 0; x < Width; x ++) {
            double px = 2.0 * Pi * (double)x / (double)Width;
            double pz = 2.0 * Pi * (double)z / (double)Depth;
            double temperature =
                0.7 * sin(px) +
                0.2 * cos(2.0 * pz) +
                0.1 * sin(px + pz);
            double vapor = 100000.0;
            if (!strcmp(scenario, "seam")) {
                temperature = x == 0 ? -10.0 : 10.0;
            } else if (!strcmp(scenario, "random")) {
                temperature = (randomUnit(&random_state) - 0.5) * 20.0;
            } else if (!strcmp(scenario, "pressure")) {
                vapor *= 1.0 + 0.5 * sin(px) * cos(pz);
            }
            tiles[indexAt(x, z)] = (Tile){
                .temperature = temperature,
                .dry = dry,
                .vapor = vapor
            };
        }
    }
}

static void thermalExchange(const Tile *tiles, Tile *next) {
    memcpy(next, tiles, sizeof(Tile) * Width * Depth);
    double factor = exchangeFactor(ThermalRate);
    for (int32_t z = 0; z < Depth; z ++) {
        for (int32_t x = 0; x < Width; x ++) {
            int32_t i = indexAt(x, z);
            int32_t right = indexAt(x + 1, z);
            int32_t bottom = indexAt(x, z + 1);
            double transfer_x =
                (tiles[right].temperature - tiles[i].temperature) * factor;
            double transfer_z =
                (tiles[bottom].temperature - tiles[i].temperature) * factor;
            next[i].temperature += transfer_x + transfer_z;
            next[right].temperature -= transfer_x;
            next[bottom].temperature -= transfer_z;
        }
    }
}

static void condensation(Tile *tiles) {
    double factor = 1.0 - exp(-CondensationRate * DeltaTime);
    double precipitation_factor =
        1.0 - exp(-PrecipitationRate * DeltaTime);
    for (int32_t i = 0; i < Width * Depth; i ++) {
        Tile *tile = &tiles[i];
        double capacity = saturationCapacity(tile->temperature);
        double excess = tile->vapor - capacity;
        if (excess > 0) {
            double condensed = excess * factor;
            double mass = gasMass(tile);
            tile->vapor -= condensed;
            tile->cloud += condensed;
            if (mass > 0) {
                tile->temperature +=
                    condensed * LatentHeat / (mass * AirHeatCapacity);
            }
        }
        double cloud_excess = tile->cloud - PrecipitationThreshold;
        if (cloud_excess > 0) {
            tile->cloud -= cloud_excess * precipitation_factor;
        }
    }
}

static void computePressure(Tile *tiles, int cloud_pressure) {
    for (int32_t i = 0; i < Width * Depth; i ++) {
        tiles[i].pressure =
            pressureMass(&tiles[i], cloud_pressure) * Gravity / CellArea;
    }
}

static void computeWind(Tile *tiles) {
    double damping = exp(-WindDrag * DeltaTime);
    for (int32_t z = 0; z < Depth; z ++) {
        for (int32_t x = 0; x < Width; x ++) {
            int32_t i = indexAt(x, z);
            Tile *tile = &tiles[i];
            double mass = gasMass(tile);
            if (mass <= 0) {
                tile->velocity_x = 0;
                tile->velocity_z = 0;
                continue;
            }
            double left = tiles[indexAt(x - 1, z)].pressure;
            double right = tiles[indexAt(x + 1, z)].pressure;
            double top = tiles[indexAt(x, z - 1)].pressure;
            double bottom = tiles[indexAt(x, z + 1)].pressure;
            double surface_area = mass * Gravity /
                (tile->pressure > 0 ? tile->pressure : 1.0);
            double force_x = (left - right) * surface_area * 0.5;
            double force_z = (top - bottom) * surface_area * 0.5;
            tile->velocity_x = (
                tile->velocity_x +
                force_x / mass * WindAcceleration * DeltaTime) * damping;
            tile->velocity_z = (
                tile->velocity_z +
                force_z / mass * WindAcceleration * DeltaTime) * damping;
            double velocity = hypot(
                tile->velocity_x, tile->velocity_z);
            if (velocity > MaxVelocity) {
                double scale = MaxVelocity / velocity;
                tile->velocity_x *= scale;
                tile->velocity_z *= scale;
            }
        }
    }
}

static void addFraction(
    const Tile *source,
    Tile *destination,
    double fraction)
{
    destination->dry += source->dry * fraction;
    destination->vapor += source->vapor * fraction;
    destination->cloud += source->cloud * fraction;
    destination->temperature +=
        source->temperature * gasMass(source) * fraction;
}

static void transportCell(const Tile *tiles, Tile *next) {
    memcpy(next, tiles, sizeof(Tile) * Width * Depth);
    for (int32_t i = 0; i < Width * Depth; i ++) {
        next[i].temperature = 0;
        next[i].dry = 0;
        next[i].vapor = 0;
        next[i].cloud = 0;
    }
    for (int32_t z = 0; z < Depth; z ++) {
        for (int32_t x = 0; x < Width; x ++) {
            int32_t i = indexAt(x, z);
            double fx = fabs(tiles[i].velocity_x) *
                DeltaTime / CellSize;
            double fz = fabs(tiles[i].velocity_z) *
                DeltaTime / CellSize;
            if (fx > 1) {
                fx = 1;
            }
            if (fz > 1) {
                fz = 1;
            }
            int32_t nx = indexAt(
                x + (tiles[i].velocity_x < 0 ? -1 : 1), z);
            int32_t nz = indexAt(
                x, z + (tiles[i].velocity_z < 0 ? -1 : 1));
            int32_t nxz = indexAt(
                x + (tiles[i].velocity_x < 0 ? -1 : 1),
                z + (tiles[i].velocity_z < 0 ? -1 : 1));
            addFraction(
                &tiles[i], &next[i], (1.0 - fx) * (1.0 - fz));
            addFraction(
                &tiles[i], &next[nx], fx * (1.0 - fz));
            addFraction(
                &tiles[i], &next[nz], (1.0 - fx) * fz);
            addFraction(
                &tiles[i], &next[nxz], fx * fz);
        }
    }
    for (int32_t i = 0; i < Width * Depth; i ++) {
        double mass = gasMass(&next[i]);
        next[i].temperature = mass > 0
            ? next[i].temperature / mass
            : tiles[i].temperature;
    }
}

static void transferFace(
    const Tile *tiles,
    double *energy,
    Tile *next,
    int32_t a,
    int32_t b,
    double velocity)
{
    int32_t source = velocity >= 0 ? a : b;
    int32_t destination = velocity >= 0 ? b : a;
    double fraction = fabs(velocity) * DeltaTime / CellSize;
    if (fraction > 0.25) {
        fraction = 0.25;
    }
    double dry = tiles[source].dry * fraction;
    double vapor = tiles[source].vapor * fraction;
    double cloud = tiles[source].cloud * fraction;
    double heat = tiles[source].temperature *
        gasMass(&tiles[source]) * fraction;
    next[source].dry -= dry;
    next[source].vapor -= vapor;
    next[source].cloud -= cloud;
    energy[source] -= heat;
    next[destination].dry += dry;
    next[destination].vapor += vapor;
    next[destination].cloud += cloud;
    energy[destination] += heat;
}

static void transportFace(const Tile *tiles, Tile *next, double *energy) {
    memcpy(next, tiles, sizeof(Tile) * Width * Depth);
    for (int32_t i = 0; i < Width * Depth; i ++) {
        energy[i] = tiles[i].temperature * gasMass(&tiles[i]);
    }
    for (int32_t z = 0; z < Depth; z ++) {
        for (int32_t x = 0; x < Width; x ++) {
            int32_t i = indexAt(x, z);
            int32_t right = indexAt(x + 1, z);
            int32_t bottom = indexAt(x, z + 1);
            double vx = 0.5 *
                (tiles[i].velocity_x + tiles[right].velocity_x);
            double vz = 0.5 *
                (tiles[i].velocity_z + tiles[bottom].velocity_z);
            transferFace(tiles, energy, next, i, right, vx);
            transferFace(tiles, energy, next, i, bottom, vz);
        }
    }
    for (int32_t i = 0; i < Width * Depth; i ++) {
        double mass = gasMass(&next[i]);
        next[i].temperature = mass > 0
            ? energy[i] / mass
            : tiles[i].temperature;
    }
}

static void report(const Tile *tiles, int32_t step) {
    double total_vapor = 0;
    double total_cloud = 0;
    double max_cloud = 0;
    double min_pressure = INFINITY;
    double max_pressure = 0;
    double max_velocity = 0;
    double rows[Depth] = {0};
    double columns[Width] = {0};
    int32_t active = 0;
    for (int32_t z = 0; z < Depth; z ++) {
        for (int32_t x = 0; x < Width; x ++) {
            const Tile *tile = &tiles[indexAt(x, z)];
            total_vapor += tile->vapor;
            total_cloud += tile->cloud;
            rows[z] += tile->cloud;
            columns[x] += tile->cloud;
            if (tile->cloud > max_cloud) {
                max_cloud = tile->cloud;
            }
            if (tile->cloud > 1.0) {
                active ++;
            }
            if (tile->pressure < min_pressure) {
                min_pressure = tile->pressure;
            }
            if (tile->pressure > max_pressure) {
                max_pressure = tile->pressure;
            }
            double velocity = hypot(
                tile->velocity_x, tile->velocity_z);
            if (velocity > max_velocity) {
                max_velocity = velocity;
            }
        }
    }
    double max_row = 0;
    double max_column = 0;
    for (int32_t z = 0; z < Depth; z ++) {
        if (rows[z] > max_row) {
            max_row = rows[z];
        }
    }
    for (int32_t x = 0; x < Width; x ++) {
        if (columns[x] > max_column) {
            max_column = columns[x];
        }
    }
    printf(
        "%.0f,%.9g,%.9g,%.9g,%.9g,%d,%.9g,%.9g,%.9g,%.9g,%.9g\n",
        step * DeltaTime,
        total_vapor,
        total_cloud,
        max_cloud,
        total_cloud > 0 ? max_cloud / total_cloud : 0,
        active,
        total_cloud > 0 ? max_row / total_cloud : 0,
        total_cloud > 0 ? max_column / total_cloud : 0,
        min_pressure,
        max_pressure,
        max_velocity);
}

int main(int argc, char *argv[]) {
    const char *transport = argc > 1 ? argv[1] : "cell";
    double dry = argc > 2 ? strtod(argv[2], NULL) : 0;
    int cloud_pressure = argc > 3 ? atoi(argv[3]) : 0;
    int32_t seconds = argc > 4 ? atoi(argv[4]) : 60;
    const char *scenario = argc > 5 ? argv[5] : "smooth";
    Tile *tiles = calloc(Width * Depth, sizeof(Tile));
    Tile *next = calloc(Width * Depth, sizeof(Tile));
    double *energy = calloc(Width * Depth, sizeof(double));
    if (!tiles || !next || !energy) {
        return 1;
    }
    initialize(tiles, dry, scenario);
    printf(
        "time,vapor_total,cloud_total,cloud_max,cell_share,"
        "active_cells,row_share,column_share,pressure_min,"
        "pressure_max,wind_max\n");
    for (int32_t step = 0; step <= seconds / DeltaTime; step ++) {
        if (step % (int32_t)(1.0 / DeltaTime) == 0) {
            report(tiles, step);
        }
        thermalExchange(tiles, next);
        Tile *swap = tiles;
        tiles = next;
        next = swap;
        condensation(tiles);
        computePressure(tiles, cloud_pressure);
        computeWind(tiles);
        if (!strcmp(transport, "face")) {
            transportFace(tiles, next, energy);
        } else {
            transportCell(tiles, next);
        }
        swap = tiles;
        tiles = next;
        next = swap;
        computePressure(tiles, cloud_pressure);
    }
    free(tiles);
    free(next);
    free(energy);
    return 0;
}
