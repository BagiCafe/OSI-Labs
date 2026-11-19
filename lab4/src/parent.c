#include "common.h"

static int wait_reply(pid_t child, const sigset_t *events) {
    for (;;) {
        int sig;
        int rc = sigwait(events, &sig);
        if (rc != 0) { fprintf(stderr, "sigwait: %s\n", strerror(rc)); return -1; }
        if (sig == SIGUSR2) return 0;
        sigset_t pending;
        if (sigpending(&pending) == -1) { perror("sigpending"); return -1; }
        if (sigismember(&pending, SIGUSR2)) continue;
        siginfo_t info;
        memset(&info, 0, sizeof(info));
        if (waitid(P_PID, (id_t)child, &info, WEXITED | WNOHANG | WNOWAIT) == -1) {
            perror("waitid"); return -1;
        }
        if (info.si_pid != 0) {
            fprintf(stderr, "Child terminated before replying\n"); return -1;
        }
    }
}

int main(void) {
    char *name = NULL, *line = NULL;
    size_t name_cap = 0, line_cap = 0;
    printf("Output file: ");
    fflush(stdout);
    if (getline(&name, &name_cap, stdin) == -1) { free(name); return 1; }
    name[strcspn(name, "\n")] = '\0';
    if (!*name) { fprintf(stderr, "Empty filename\n"); free(name); return 1; }

    sigset_t events;
    sigemptyset(&events);
    sigaddset(&events, SIGUSR1);
    sigaddset(&events, SIGUSR2);
    sigaddset(&events, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &events, NULL) == -1) { perror("sigprocmask"); free(name); return 1; }  // Блокируем сигналы до fork
    sigdelset(&events, SIGUSR1);

    char tmp[] = "/tmp/osi-lab4-XXXXXX";
    int fd = mkstemp(tmp);  // Создаём временный файл для обмена
    if (fd == -1) { perror("mkstemp"); free(name); return 1; }
    if (unlink(tmp) == -1) { perror("unlink"); close(fd); free(name); return 1; }
    if (ftruncate(fd, sizeof(message_t) + 1) == -1) { perror("ftruncate"); close(fd); free(name); return 1; }
    pid_t child = fork();  // Создаём дочерний процесс
    if (child == -1) { perror("fork"); close(fd); free(name); return 1; }
    if (child == 0) {
        char number[32];
        snprintf(number, sizeof(number), "%d", fd);
        execl("./child", "child", name, number, (char *)NULL);  // Запускаем child
        perror("execl");
        _exit(1);
    }
    free(name);
    int result = 0;
    if (wait_reply(child, &events) == -1) result = 1;
    size_t size;
    message_t *m = NULL;
    if (!result) {
        m = map_file(fd, &size);
        if (!m) result = 1;
        else {
            if (m->status != OK) result = 1;
            if (unmap_file(m, size) == -1) result = 1;
        }
    }
    while (!result) {
        printf("Integers (Ctrl+D to finish): ");
        fflush(stdout);
        errno = 0;
        ssize_t len = getline(&line, &line_cap, stdin);  // Читаем числа с клавиатуры
        int stop = len == -1;
        if (stop && ferror(stdin)) { perror("getline"); result = 1; break; }
        size_t bytes = sizeof(message_t) + (stop ? 1 : (size_t)len + 1);
        if (ftruncate(fd, (off_t)bytes) == -1) { perror("ftruncate"); result = 1; break; }  // Меняем размер временного файла
        m = map_file(fd, &size);  // Отображаем общий файл у родителя
        if (!m) { result = 1; break; }
        m->status = stop ? STOP : OK;
        if (!stop) memcpy(m->text, line, (size_t)len + 1);  // Копируем строку в общий файл
        if (unmap_file(m, size) == -1) { result = 1; break; }  // Снимаем отображение родителя
        if (kill(child, SIGUSR1) == -1) { perror("kill"); result = 1; break; }  // SIGUSR1: команда готова
        if (wait_reply(child, &events) == -1) { result = 1; break; }  // Ждём ответ ребёнка
        m = map_file(fd, &size);
        if (!m) { result = 1; break; }
        int status = m->status;  // Читаем статус из общего файла
        if (unmap_file(m, size) == -1) { result = 1; break; }
        if (status == DIV_ZERO) { fprintf(stderr, "Division by zero\n"); result = 2; break; }
        if (status == IO_ERROR) { result = 1; break; }
        if (status == BAD_INPUT) fprintf(stderr, "Invalid integers or int overflow\n");
        if (stop) break;
    }
    free(line);
    if (result == 1 && kill(child, SIGTERM) == -1 && errno != ESRCH) perror("kill");
    int status;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited == -1 && errno == EINTR);  // Ждём завершения ребёнка.
    if (waited == -1) { perror("waitpid"); result = 1; }
    else if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0 && result == 0)) result = 1;
    if (close(fd) == -1) { perror("close"); result = 1; }
    return result;
}
