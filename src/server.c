#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
#include "server.h"

int main(void) {
    int listenfd, connfd;
    struct sockaddr_in srv;
    int opt = 1;
    int clients[FD_SETSIZE];
    int i, maxi = -1, nready, maxfd;
    fd_set rset, allset;

    for (i = 0; i < FD_SETSIZE; i++) clients[i] = -1;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); exit(1); }

    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&srv, 0, sizeof(srv));
    srv.sin_family      = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port        = htons(PORT);

    if (bind(listenfd, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(listenfd, BACKLOG) < 0) {
        perror("listen"); exit(1);
    }

    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    maxfd = listenfd;

    printf("Serveur select démarré sur le port %d\n", PORT);

    while (1) {
        rset = allset;
        struct timeval tv = {5, 0};
        nready = select(maxfd + 1, &rset, NULL, NULL, &tv);
        if (nready < 0) { perror("select"); continue; }
        if (nready == 0) { printf("Timeout, en attente...\n"); continue; }

        if (FD_ISSET(listenfd, &rset)) {
            connfd = accept(listenfd, NULL, NULL);
            for (i = 0; i < FD_SETSIZE; i++) {
                if (clients[i] == -1) {
                    clients[i] = connfd;
                    if (i > maxi) maxi = i;
                    break;
                }
            }
            FD_SET(connfd, &allset);
            if (connfd > maxfd) maxfd = connfd;
            if (--nready <= 0) continue;
        }

        for (i = 0; i <= maxi; i++) {
            if (clients[i] == -1) continue;
            if (FD_ISSET(clients[i], &rset)) {
                char buf[BUF_SIZE];
                char response[BUF_SIZE + 64];
                ssize_t n = read(clients[i], buf, sizeof(buf) - 1);
                if (n <= 0) {
                    close(clients[i]);
                    FD_CLR(clients[i], &allset);
                    clients[i] = -1;
                } else {
                    buf[n] = '\0';
                    snprintf(response, sizeof(response), "Echo : %s", buf);
                    write(clients[i], response, strlen(response));
                }

                int count = 0;
                for (int j = 0; j < FD_SETSIZE; j++)
                    if (clients[j] != -1) count++;
                printf("Descripteurs surveillés : %d\n", count);

                if (--nready <= 0) break;
            }
        }
    }
    return 0;
}
