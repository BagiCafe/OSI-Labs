#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

static void reply(int fd, const char *msg) {
    if (write(fd, msg, strlen(msg)) == -1)
        perror("child: write pipe2");
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Использование: child <файл> <fd_pipe2>\n");
        return 1;
    }
    const char *fileName = argv[1];
    int pipe2fd = atoi(argv[2]);

    FILE *out = fopen(fileName, "w");
    if (out == NULL) {
        perror("child: fopen");
        reply(pipe2fd, "ERR не удалось открыть файл\n");
        return 1;
    }

    char *line = NULL;
    size_t cap = 0;

    while (getline(&line, &cap, stdin) != -1) {
        char *p = line;
        char *end;
        int count = 0;
        long long result = 0;
        int bad = 0;
        int div0 = 0;

        while (1) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\n' || *p == '\0') break;

            errno = 0;
            long v = strtol(p, &end, 10);
            if (end == p || errno == ERANGE || v > INT_MAX || v < INT_MIN ||
                (*end != ' ' && *end != '\t' && *end != '\n' && *end != '\0')) {
                bad = 1;
                break;
            }
            p = end;

            if (count == 0) {
                result = v;
            } else {
                if (v == 0) {
                    div0 = 1;
                    break;
                }
                result /= v;
            }
            count++;
        }

        if (div0) {
            fprintf(out, "Деление на 0, завершение работы\n");
            fclose(out);
            reply(pipe2fd, "DIV0\n");
            free(line);
            return 3;
        }
        if (bad || count == 0) {
            reply(pipe2fd, "ERR некорректная строка\n");
            continue;
        }
        if (result > INT_MAX || result < INT_MIN) {
            reply(pipe2fd, "ERR переполнение int\n");
            continue;
        }

        fprintf(out, "%lld\n", result);
        fflush(out);
        reply(pipe2fd, "OK\n");
    }

    free(line);
    fclose(out);
    close(pipe2fd);
    return 0;
}
