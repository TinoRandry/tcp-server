#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <syslog.h>
#include "server.h"

/*
 * get_client_ip - Récupère l'adresse IP d'un client
 * @addr : structure sockaddr_in du client
 * @buf  : buffer de sortie
 * @len  : taille du buffer
 * Retour : pointeur vers buf
 */
char *get_client_ip(struct sockaddr_in *addr, char *buf, int len) {
    inet_ntop(AF_INET, &addr->sin_addr, buf, len);
    return buf;
}

/*
 * write_pid - Écrit le PID courant dans un fichier
 * @pidfile : chemin du fichier PID
 * Retour : 0 si succès, -1 si erreur
 */
int write_pid(const char *pidfile) {
    FILE *f = fopen(pidfile, "w");
    if (!f) return -1;
    fprintf(f, "%d\n", getpid());
    fclose(f);
    return 0;
}

/*
 * read_pid - Lit le PID depuis un fichier
 * @pidfile : chemin du fichier PID
 * Retour : PID lu, -1 si erreur
 */
int read_pid(const char *pidfile) {
    FILE *f = fopen(pidfile, "r");
    if (!f) return -1;
    int pid;
    fscanf(f, "%d", &pid);
    fclose(f);
    return pid;
}
