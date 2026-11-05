#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#define PAD 128

typedef struct {
    int k;
    size_t lo;
    size_t hi;
    const double *x;
    const double *y;
    const double *cx;
    const double *cy;
    int *labels;
    double *sumX;
    double *sumY;
    long *count;
    long changed;
} task_t;

static int nearest_center(double px, double py, const double *cx, const double *cy, int k) {
    int best = 0;
    double dx = px - cx[0];
    double dy = py - cy[0];
    double bestDist = dx * dx + dy * dy;
    for (int c = 1; c < k; c++) {
        dx = px - cx[c];
        dy = py - cy[c];
        double d = dx * dx + dy * dy;
        if (d < bestDist) {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

static void *worker(void *arg) {
    task_t *t = arg;

    for (int c = 0; c < t->k; c++) {
        t->sumX[c] = 0.0;
        t->sumY[c] = 0.0;
        t->count[c] = 0;
    }
    t->changed = 0;

    for (size_t i = t->lo; i < t->hi; i++) {
        int best = nearest_center(t->x[i], t->y[i], t->cx, t->cy, t->k);
        if (t->labels[i] != best) {
            t->labels[i] = best;
            t->changed++;
        }
        t->sumX[best] += t->x[i];
        t->sumY[best] += t->y[i];
        t->count[best]++;
    }
    return NULL;
}

static int run_iteration(task_t *tasks, pthread_t *ids, int workers) {
    int created = 0;
    int failed = 0;

    for (int i = 0; i < workers; i++) {
        int rc = pthread_create(&ids[i], NULL, worker, &tasks[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            failed = 1;
            break;
        }
        created++;
    }

    for (int i = 0; i < created; i++) {
        int rc = pthread_join(ids[i], NULL);
        if (rc != 0) {
            fprintf(stderr, "pthread_join: %s\n", strerror(rc));
            failed = 1;
        }
    }
    return failed ? -1 : 0;
}

static void update_centers(const task_t *tasks, int workers, int k, double *cx, double *cy, long *sizes) {
    for (int c = 0; c < k; c++) {
        double sumX = 0.0;
        double sumY = 0.0;
        long count = 0;
        for (int i = 0; i < workers; i++) {
            sumX += tasks[i].sumX[c];
            sumY += tasks[i].sumY[c];
            count += tasks[i].count[c];
        }
        sizes[c] = count;
        if (count > 0) {
            cx[c] = sumX / count;
            cy[c] = sumY / count;
        }
    }
}

static int parse_point(const char *line, double *x, double *y) {
    const char *p = line;
    char *end;

    while (isspace((unsigned char)*p)) p++;
    if (*p == '\0') return 1;

    *x = strtod(p, &end);
    if (end == p) return -1;
    p = end;

    *y = strtod(p, &end);
    if (end == p) return -1;
    p = end;

    while (isspace((unsigned char)*p)) p++;
    return *p == '\0' ? 0 : -1;
}

static int read_points(const char *path, double **xs, double **ys, size_t *nOut) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        perror(path);
        return -1;
    }

    double *x = NULL;
    double *y = NULL;
    size_t n = 0;
    size_t capacity = 0;
    size_t lineNo = 0;
    char *line = NULL;
    size_t lineCap = 0;
    int ok = 1;

    while (getline(&line, &lineCap, f) != -1) {
        lineNo++;
        double vx, vy;
        int r = parse_point(line, &vx, &vy);
        if (r == 1) continue;
        if (r == -1) {
            fprintf(stderr, "%s: строка %zu: ожидалось «x y»\n", path, lineNo);
            ok = 0;
            break;
        }

        if (n == capacity) {
            size_t newCapacity = capacity ? capacity * 2 : 1024;
            double *nx = realloc(x, newCapacity * sizeof(double));
            if (nx != NULL) x = nx;
            double *ny = realloc(y, newCapacity * sizeof(double));
            if (ny != NULL) y = ny;
            if (nx == NULL || ny == NULL) {
                fprintf(stderr, "Не хватает памяти для точек\n");
                ok = 0;
                break;
            }
            capacity = newCapacity;
        }
        x[n] = vx;
        y[n] = vy;
        n++;
    }

    free(line);
    fclose(f);

    if (!ok) {
        free(x);
        free(y);
        return -1;
    }
    *xs = x;
    *ys = y;
    *nOut = n;
    return 0;
}

static int parse_int(const char *s, int min, int max, int *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE || v < min || v > max) return -1;
    *out = (int)v;
    return 0;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void usage(const char *prog) {
    fprintf(stderr, "Использование: %s -i <файл с точками> [-t потоки] [-m макс. итераций] [-o файл меток]\n", prog);
}

int main(int argc, char *argv[]) {
    int threads = 1;
    int maxIter = 1000;
    const char *inPath = NULL;
    const char *outPath = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "t:i:m:o:")) != -1) {
        switch (opt) {
            case 't':
                if (parse_int(optarg, 1, 1024, &threads) == -1) {
                    fprintf(stderr, "Ключ -t: нужно целое от 1 до 1024\n");
                    return 1;
                }
                break;
            case 'm':
                if (parse_int(optarg, 1, 1000000, &maxIter) == -1) {
                    fprintf(stderr, "Ключ -m: нужно целое от 1 до 1000000\n");
                    return 1;
                }
                break;
            case 'i':
                inPath = optarg;
                break;
            case 'o':
                outPath = optarg;
                break;
            default:
                usage(argv[0]);
                return 1;
        }
    }
    if (inPath == NULL) {
        usage(argv[0]);
        return 1;
    }

    double *x, *y;
    size_t n;
    if (read_points(inPath, &x, &y, &n) == -1) return 1;
    if (n == 0) {
        fprintf(stderr, "В файле нет точек\n");
        return 1;
    }

    printf("Прочитано точек: %zu\n", n);
    printf("Введите число кластеров (1..%zu): ", n);
    fflush(stdout);
    char buf[64];
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        fprintf(stderr, "Не удалось прочитать число кластеров\n");
        return 1;
    }
    buf[strcspn(buf, "\n")] = '\0';
    int maxK = n > 1000000 ? 1000000 : (int)n;
    int k;
    if (parse_int(buf, 1, maxK, &k) == -1) {
        fprintf(stderr, "Число кластеров должно быть целым от 1 до %d\n", maxK);
        return 1;
    }

    int workers = (size_t)threads > n ? (int)n : threads;

    int *labels = malloc(n * sizeof(int));
    double *cx = malloc(k * sizeof(double));
    double *cy = malloc(k * sizeof(double));
    long *sizes = malloc(k * sizeof(long));
    pthread_t *ids = malloc(workers * sizeof(pthread_t));
    task_t *tasks = calloc(workers, sizeof(task_t));
    if (!labels || !cx || !cy || !sizes || !ids || !tasks) {
        fprintf(stderr, "Не хватает памяти\n");
        return 1;
    }

    for (size_t i = 0; i < n; i++) labels[i] = -1;
    for (int c = 0; c < k; c++) {
        size_t idx = (size_t)c * n / k;
        cx[c] = x[idx];
        cy[c] = y[idx];
    }

    for (int i = 0; i < workers; i++) {
        tasks[i].k = k;
        tasks[i].lo = (size_t)i * n / workers;
        tasks[i].hi = (size_t)(i + 1) * n / workers;
        tasks[i].x = x;
        tasks[i].y = y;
        tasks[i].cx = cx;
        tasks[i].cy = cy;
        tasks[i].labels = labels;
        tasks[i].sumX = malloc(k * sizeof(double) + PAD);
        tasks[i].sumY = malloc(k * sizeof(double) + PAD);
        tasks[i].count = malloc(k * sizeof(long) + PAD);
        if (!tasks[i].sumX || !tasks[i].sumY || !tasks[i].count) {
            fprintf(stderr, "Не хватает памяти\n");
            return 1;
        }
    }

    double start = now_ms();

    int iter = 0;
    int converged = 0;
    while (iter < maxIter && !converged) {
        if (run_iteration(tasks, ids, workers) == -1) return 1;
        iter++;

        long changed = 0;
        for (int i = 0; i < workers; i++) changed += tasks[i].changed;

        update_centers(tasks, workers, k, cx, cy, sizes);
        converged = (changed == 0);
    }

    double elapsed = now_ms() - start;

    double sse = 0.0;
    for (size_t i = 0; i < n; i++) {
        double dx = x[i] - cx[labels[i]];
        double dy = y[i] - cy[labels[i]];
        sse += dx * dx + dy * dy;
    }

    printf("Потоков: %d\n", workers);
    printf("Итераций: %d (%s)\n", iter, converged ? "сошлось" : "достигнут лимит");
    printf("Время: %.3f мс\n", elapsed);
    printf("SSE: %.3f\n", sse);
    for (int c = 0; c < k; c++)
        printf("Кластер %d: центр (%.4f, %.4f), точек: %ld\n", c, cx[c], cy[c], sizes[c]);

    if (outPath != NULL) {
        FILE *out = fopen(outPath, "w");
        if (out == NULL) {
            perror(outPath);
            return 1;
        }
        for (size_t i = 0; i < n; i++)
            fprintf(out, "%.6f %.6f %d\n", x[i], y[i], labels[i]);
        if (fclose(out) != 0) {
            perror(outPath);
            return 1;
        }
    }

    for (int i = 0; i < workers; i++) {
        free(tasks[i].sumX);
        free(tasks[i].sumY);
        free(tasks[i].count);
    }
    free(tasks);
    free(ids);
    free(sizes);
    free(cx);
    free(cy);
    free(labels);
    free(x);
    free(y);
    return 0;
}
