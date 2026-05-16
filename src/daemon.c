#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <syslog.h>

/*
 * daemonize - Transforme le processus en daemon UNIX
 * @pidfile : chemin du fichier PID
 * Retour   : void
 */
void daemonize(const char *pidfile) {
    pid_t pid;

    /* 1er fork */
    if ((pid = fork()) < 0) { perror("fork"); exit(1); }
    if (pid > 0) exit(0);

    setsid();

    /* 2ème fork */
    if ((pid = fork()) < 0) { perror("fork"); exit(1); }
    if (pid > 0) exit(0);

    chdir("/");
    umask(0);

    /* Rediriger vers /dev/null */
    int fd = open("/dev/null", O_RDWR);
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > 2) close(fd);

    /* Détection double instance */
    FILE *f = fopen(pidfile, "r");
    if (f) {
        int old_pid;
        fscanf(f, "%d", &old_pid);
        fclose(f);
        if (kill(old_pid, 0) == 0) {
            syslog(LOG_ERR, "Daemon déjà en cours (PID=%d)", old_pid);
            exit(1);
        }
    }

    /* Écrire le PID */
    f = fopen(pidfile, "w");
    if (f) { fprintf(f, "%d\n", getpid()); fclose(f); }
}
