#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <syslog.h>
#include <arpa/inet.h>
#include "server.h"

static int *connexions_actives;

static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        (*connexions_actives)--;
}

int main(void) {
    int listenfd, connfd;
    struct sockaddr_in srv, cli;
    socklen_t clilen = sizeof(cli);
    int conn_num = 0;
    int opt = 1;

    openlog("myserverd", LOG_PID | LOG_CONS, LOG_DAEMON);

    daemonize("/tmp/myserverd.pid");

    connexions_actives = mmap(NULL, sizeof(int),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *connexions_actives = 0;

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { syslog(LOG_ERR, "socket() échoué : %m"); exit(1); }

    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&srv, 0, sizeof(srv));
    srv.sin_family      = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port        = htons(PORT);

    if (bind(listenfd, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        syslog(LOG_ERR, "bind() échoué : %m"); exit(1);
    }
    if (listen(listenfd, BACKLOG) < 0) {
        syslog(LOG_ERR, "listen() échoué : %m"); exit(1);
    }

    syslog(LOG_INFO, "Daemon démarré sur le port %d", PORT);

    while (1) {
        connfd = accept(listenfd, (struct sockaddr*)&cli, &clilen);
        if (connfd < 0) { syslog(LOG_WARNING, "accept() échoué : %m"); continue; }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        syslog(LOG_INFO, "Connexion acceptée de %s:%d", ip, ntohs(cli.sin_port));

        pid_t pid = fork();
        if (pid < 0) { syslog(LOG_ERR, "fork() échoué : %m"); close(connfd); continue; }

        if (pid == 0) {
            close(listenfd);
            (*connexions_actives)++;
            handle_client_iter(connfd, ++conn_num);
            syslog(LOG_INFO, "Client traité (fd=%d)", connfd);
            close(connfd);
            exit(0);
        }
        close(connfd);
    }

    closelog();
    return 0;
}
