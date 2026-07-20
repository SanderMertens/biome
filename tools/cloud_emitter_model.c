#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define Width (128)
#define Depth (128)
#define Capacity (65536)
#define DeltaTime (1.0 / 60.0)
#define EmitChance (0.35)
#define MinAge (15.0)
#define AgeVariance (25.0)

typedef struct Particle {
    int32_t cell;
    double age;
    double max_age;
} Particle;

static double randomUnit(uint32_t *state) {
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return (double)(value & 0xffffff) / (double)0x1000000;
}

static int32_t greatestCommonDivisor(int32_t a, int32_t b) {
    while (b) {
        int32_t next = a % b;
        a = b;
        b = next;
    }
    return a;
}

static int32_t randomStride(uint32_t *state, int32_t cells) {
    int32_t stride = 1 +
        (int32_t)randomUnit(state) * (cells - 1);
    while (greatestCommonDivisor(stride, cells) != 1) {
        stride ++;
        if (stride >= cells) {
            stride = 1;
        }
    }
    return stride;
}

static void updateParticles(Particle *particles, int32_t *count) {
    for (int32_t i = 0; i < *count; i ++) {
        particles[i].age += DeltaTime;
        if (particles[i].age >= particles[i].max_age) {
            particles[i] = particles[-- *count];
            i --;
        }
    }
}

static void emit(
    Particle *particles,
    int32_t *count,
    int32_t cell,
    uint32_t *state)
{
    if (*count >= Capacity || randomUnit(state) >= EmitChance) {
        return;
    }
    particles[(*count) ++] = (Particle){
        .cell = cell,
        .max_age = MinAge + randomUnit(state) * AgeVariance
    };
}

static void run(int randomized) {
    Particle *particles = calloc(Capacity, sizeof(Particle));
    int32_t count = 0;
    uint32_t state = 0x51ed270bu;
    int32_t cells = Width * Depth;
    for (int32_t frame = 0; frame < 120 / DeltaTime; frame ++) {
        updateParticles(particles, &count);
        int32_t start = randomized
            ? (int32_t)(randomUnit(&state) * cells)
            : 0;
        int32_t stride = randomized
            ? randomStride(&state, cells)
            : 1;
        for (int32_t j = 0; j < cells && count < Capacity; j ++) {
            int32_t cell = (int32_t)(
                (start + (int64_t)j * stride) % cells);
            emit(particles, &count, cell, &state);
        }
    }

    int32_t rows[Depth] = {0};
    int32_t active[Width * Depth] = {0};
    for (int32_t i = 0; i < count; i ++) {
        int32_t cell = particles[i].cell;
        rows[cell / Width] ++;
        active[cell] = 1;
    }
    int32_t active_cells = 0;
    int32_t max_row = 0;
    for (int32_t i = 0; i < cells; i ++) {
        active_cells += active[i];
    }
    for (int32_t z = 0; z < Depth; z ++) {
        if (rows[z] > max_row) {
            max_row = rows[z];
        }
    }
    printf(
        "%s,count=%d,active_cells=%d,max_row_share=%.6f\n",
        randomized ? "randomized" : "fixed",
        count,
        active_cells,
        count ? (double)max_row / (double)count : 0);
    free(particles);
}

int main(void) {
    run(0);
    run(1);
    return 0;
}
