#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

#define PI 3.14159265358979323846

static uint64_t state;

static uint64_t next_u64(void) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

static double next_unit(void) {
    return (next_u64() >> 11) * (1.0 / 9007199254740992.0);
}

static double next_gauss(void) {
    double u1 = next_unit();
    double u2 = next_unit();
    if (u1 < 1e-12) u1 = 1e-12;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * PI * u2);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Использование: %s <точек> <кластеров> <seed>\n", argv[0]);
        return 1;
    }
    long n = atol(argv[1]);
    int blobs = atoi(argv[2]);
    long seed = atol(argv[3]);
    if (n <= 0 || blobs <= 0) {
        fprintf(stderr, "Параметры должны быть положительными\n");
        return 1;
    }
    state = 88172645463325252ULL ^ ((uint64_t)seed * 2654435761ULL);
    for (int i = 0; i < 10; i++) next_u64();

    double *bx = malloc(blobs * sizeof(double));
    double *by = malloc(blobs * sizeof(double));
    if (!bx || !by) {
        fprintf(stderr, "Не хватает памяти\n");
        return 1;
    }
    for (int b = 0; b < blobs; b++) {
        bx[b] = next_unit() * 1000.0;
        by[b] = next_unit() * 1000.0;
    }
    for (long i = 0; i < n; i++) {
        int b = (int)(next_u64() % (uint64_t)blobs);
        printf("%.6f %.6f\n", bx[b] + 25.0 * next_gauss(), by[b] + 25.0 * next_gauss());
    }
    free(bx);
    free(by);
    return 0;
}
