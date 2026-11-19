#include "common.h"

static int divide_line(const char *p, int *answer) {
    int first = 1;
    int value = 0;
    for (;;) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char *end;
        errno = 0;
        long n = strtol(p, &end, 10);
        if (p == end || errno == ERANGE || n < INT_MIN || n > INT_MAX ||
            (*end && !isspace((unsigned char)*end))) return BAD_INPUT;
        if (first) { value = (int)n; first = 0; }
        else {
            if (n == 0) return DIV_ZERO;  // Проверяем делитель до деления
            if (value == INT_MIN && n == -1) return BAD_INPUT;
            value /= (int)n;
        }
        p = end;
    }
    if (first) return BAD_INPUT;
    *answer = value;
    return OK;
}

int main(int argc, char **argv) {
    if (argc != 3) return 1;
    char *end;
    errno = 0;
    long number = strtol(argv[2], &end, 10);
    if (*end || errno || number < 0 || number > INT_MAX) return 1;
    int fd = (int)number;
    pid_t parent = getppid();
    sigset_t request;
    sigemptyset(&request);
    sigaddset(&request, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &request, NULL) == -1) { perror("sigprocmask"); return 1; }
    FILE *out = fopen(argv[1], "w");  // Создаём или очищаем файл с результатом
    if (!out) perror("fopen");
    size_t size;
    message_t *m = map_file(fd, &size);  // Отображаем временный файл
    if (!m) { if (out) fclose(out); return 1; }
    m->status = out ? OK : IO_ERROR;  // Сообщаем, удалось ли открыть результат
    if (unmap_file(m, size) == -1) { if (out) fclose(out); return 1; }
    if (kill(parent, SIGUSR2) == -1) { perror("kill"); if (out) fclose(out); return 1; }
    if (!out) return 1;
    int result = 0;
    for (;;) {
        int sig;
        int rc = sigwait(&request, &sig);  // Ждём команду от родителя
        if (rc) { fprintf(stderr, "sigwait: %s\n", strerror(rc)); result = 1; break; }
        m = map_file(fd, &size);  // Отображаем тот же общий файл.
        if (!m) { result = 1; break; }
        int status = m->status;
        if (status != STOP) {
            int answer;
            status = divide_line(m->text, &answer);  // Читаем строку из общего файла
            if (status == OK && (fprintf(out, "%d\n", answer) < 0 || fflush(out) == EOF)) {  // Пишем число в файл с результатом
                perror("write result"); status = IO_ERROR;
            }
        }
        m->status = status;  // Пишем статус в общий файл
        if (unmap_file(m, size) == -1) { result = 1; break; }  // Снимаем отображение ребёнка
        if (kill(parent, SIGUSR2) == -1) { perror("kill"); result = 1; break; }  // SIGUSR2: ответ готов
        if (status == DIV_ZERO) { result = 3; break; }
        if (status == IO_ERROR) { result = 1; break; }
        if (status == STOP) break;
    }
    if (fclose(out) == EOF) { perror("fclose"); result = 1; }
    if (close(fd) == -1) { perror("close"); result = 1; }
    return result;
}
