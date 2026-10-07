#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#define BUF_SIZE 4096

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    char fileName[256];
    printf("Введите имя файла: ");
    fflush(stdout);
    if (fgets(fileName, sizeof(fileName), stdin) == NULL) {
        fprintf(stderr, "Не удалось прочитать имя файла\n");
        return 1;
    }
    fileName[strcspn(fileName, "\n")] = '\0';
    if (fileName[0] == '\0') {
        fprintf(stderr, "Имя файла не может быть пустым\n");
        return 1;
    }

    int pipe1[2];
    int pipe2[2];
    if (pipe(pipe1) == -1) { perror("pipe1"); return 1; }
    if (pipe(pipe2) == -1) { perror("pipe2"); return 1; }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        close(pipe1[1]);
        close(pipe2[0]);

        if (dup2(pipe1[0], STDIN_FILENO) == -1) {
            perror("dup2");
            _exit(1);
        }
        close(pipe1[0]);

        char fdStr[16];
        snprintf(fdStr, sizeof(fdStr), "%d", pipe2[1]);

        execl("./child", "child", fileName, fdStr, (char *)NULL);
        perror("execl");
        _exit(1);
    }

    close(pipe1[0]);
    close(pipe2[1]);

    printf("Вводите строки вида «число число число» (Ctrl+D - выход)\n");

    char line[BUF_SIZE];
    int exitCode = 0;

    while (fgets(line, sizeof(line), stdin) != NULL) {
        if (line[0] == '\n') continue;
        size_t len = strlen(line);
        if (len == 0) continue;
        if (line[len - 1] != '\n') {
            if (len + 1 < sizeof(line)) { line[len++] = '\n'; line[len] = '\0'; }
        }

        ssize_t w = write(pipe1[1], line, len);
        if (w == -1) {
            if (errno == EPIPE) {
                fprintf(stderr, "Дочерний процесс завершился, выходим\n");
            } else {
                perror("write pipe1");
            }
            exitCode = 1;
            break;
        }

        char reply[256];
        ssize_t r = read(pipe2[0], reply, sizeof(reply) - 1);
        if (r == -1) {
            perror("read pipe2");
            exitCode = 1;
            break;
        }
        if (r == 0) {
            fprintf(stderr, "Дочерний процесс завершился\n");
            exitCode = 1;
            break;
        }
        reply[r] = '\0';

        if (strncmp(reply, "DIV0", 4) == 0) {
            printf("Деление на 0! Завершаем работу.\n");
            exitCode = 2;
            break;
        } else if (strncmp(reply, "ERR", 3) == 0) {
            printf("Ошибка в строке: %s", reply + 4);
        }
    }

    close(pipe1[1]);
    close(pipe2[0]);

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return 1;
    }
    if (WIFEXITED(status))
        printf("Дочерний процесс завершился с кодом %d\n", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        printf("Дочерний процесс убит сигналом %d\n", WTERMSIG(status));

    return exitCode;
}
