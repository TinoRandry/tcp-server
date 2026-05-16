# Conception, analyse et comparaison de différents modèles de serveurs TCP en C

**Auteurs :**
- FIONONANA RANDRY Tino (N° 6 - TCO)
- RAFANOMEZANTSOA Holinirina Vahatriniaina (N° 10 - TCO)
- RAKOTOVAO Nantenaina Elvys (N° 18 - TCO)

**Licence 3 Télécommunications**

---

## 1. Introduction

L'objectif de cette expérience est de démontrer et de comparer les différents
modèles de serveurs TCP en C. Dans ce rapport, le modèle itératif, concurrent
avec fork, pthreads, et le multiplexage I/O seront particulièrement discutés.
Le serveur sera exécuté en tant que daemon et les logs seront gérés avec syslog.

### Choix architectural global

Nous avons implémenté 4 modèles de serveurs TCP en ordre croissant de complexité :

- Modèle itératif : point de départ simple, un client à la fois
- Modèle fork : parallélisme par processus, isolation totale entre clients
- Modèle pthreads : parallélisme par threads, mémoire partagée, plus léger
- Modèle select : mono-thread, multiplexage I/O, base des serveurs modernes

Ce choix permet de comprendre les compromis entre simplicité, performance,
isolation et consommation mémoire. En production, le modèle epoll + thread pool
(utilisé par nginx) serait préféré pour sa scalabilité.

### Compilation et lancement

    make
    ./tcp_server

---

## 2. Modèle itératif

### 2.1 Principe

Dans le modèle itératif, les clients sont servis un par un dans l'ordre des
connexions. Le serveur est bloqué sur read() du client courant et ne peut pas
accepter de nouvelle connexion tant que le client n'a pas terminé.

### 2.2 Démonstration

#### 2.2.1 Test sur un seul client

    # Terminal 1 — lancer le serveur
    $ ./tcp_server
    Serveur itératif démarré sur le port 9999

    # Terminal 2 — lancer le client
    $ nc 127.0.0.1 9999
    hello
    [Connexion #1] Echo : hello

#### 2.2.2 Cas de deux clients

    # Terminal 2 — client 1 silencieux
    $ nc 127.0.0.1 9999
    (ne tape rien, garde la connexion ouverte)

    # Terminal 3 — client 2
    $ echo "Hello" | nc 127.0.0.1 9999
    (bloqué, pas de réponse tant que client 1 est connecté)

    # Quand client 1 fait Ctrl+C, client 2 reçoit :
    [Connexion #2] Echo : Hello

Le serveur est bloqué sur read() du client 1 et ne peut pas accepter de
nouvelle connexion. Le client 2 reste dans la file d'attente du noyau (backlog).

---

## 3. Modèle concurrent avec fork()

### 3.1 Principe

Chaque connexion cliente est prise en charge par un processus fils indépendant
créé avec fork(). Le père reste en écoute permanente et ferme immédiatement
connfd après le fork. Un gestionnaire SIGCHLD avec waitpid(-1, NULL, WNOHANG)
évite les processus zombies. Le compteur de connexions actives est partagé via
mmap MAP_SHARED | MAP_ANONYMOUS car cette zone mémoire est partagée entre le
père et tous les fils avant fork(), contrairement aux variables globales
copiées à chaque fork().

### 3.2 Démonstration

#### 3.2.1 Test avec 8 clients simultanés

    # Terminal 1 — lancer le serveur
    $ ./tcp_server
    Serveur fork démarré sur le port 9999

    # Terminal 2 — lancer 8 clients simultanés
    $ for i in $(seq 1 8); do
        (echo "Client $i : bonjour" | nc -q 1 127.0.0.1 9999) &
      done
      wait
    [Connexion #1] Echo : Client 2 : bonjour
    [Connexion #1] Echo : Client 4 : bonjour
    [Connexion #1] Echo : Client 1 : bonjour
    [Connexion #1] Echo : Client 7 : bonjour
    [Connexion #1] Echo : Client 3 : bonjour
    [Connexion #1] Echo : Client 6 : bonjour
    [Connexion #1] Echo : Client 5 : bonjour
    [Connexion #1] Echo : Client 8 : bonjour
    Tous les clients ont terminé

Les clients arrivent dans le désordre ce qui prouve le parallélisme.

#### 3.2.2 Arbre de processus observé avec ps

    $ pstree -p $(pgrep tcp_server)
    tcp_server(9094)

    Père (PID 9094) — listen() en permanence
    ├── Fils — gère Client 1 → terminé rapidement
    ├── Fils — gère Client 2 → terminé rapidement
    └── ...

    $ ps -C tcp_server -o rss=
    1660 KB

#### 3.2.3 Test anti-zombies

    $ ps aux | grep Z
    Aucun zombie lié à tcp_server.
    Le gestionnaire SIGCHLD nettoie correctement les fils.

---

## 4. Modèle multi-threadé avec pthreads

### 4.1 Principe

Chaque connexion est gérée par un thread indépendant. Un mutex protège le
compteur connexions_actives. Un pool de MAX_THREADS=16 refuse les connexions
si saturé. pthread_detach() libère automatiquement les ressources du thread
à sa fin. Le descripteur connfd est passé via malloc() pour éviter toute
race condition — passer &connfd directement causerait une race condition car
connfd est réécrit à la prochaine itération avant que le thread ait eu le
temps de le lire.

### 4.2 Démonstration

#### 4.2.1 Test avec 8 clients simultanés

    # Terminal 1 — lancer le serveur
    $ ./tcp_server
    Serveur threads démarré sur le port 9999
    Connexions actives : 1 / 16
    Connexions actives : 2 / 16
    ...

    # Terminal 2 — lancer 8 clients simultanés
    $ for i in $(seq 1 8); do
        (echo "Client $i : bonjour" | nc -q 1 127.0.0.1 9999) &
      done
      wait
    [Connexion #1] Echo : Client 2 : bonjour
    [Connexion #2] Echo : Client 5 : bonjour
    [Connexion #4] Echo : Client 1 : bonjour
    [Connexion #6] Echo : Client 7 : bonjour
    [Connexion #7] Echo : Client 4 : bonjour
    [Connexion #3] Echo : Client 3 : bonjour
    [Connexion #5] Echo : Client 6 : bonjour
    [Connexion #8] Echo : Client 8 : bonjour

Les numéros de connexion sont différents car le mutex protège le compteur.

#### 4.2.2 Mesure mémoire

    $ cat /proc/$(pgrep tcp_server)/status | grep VmRSS
    VmRSS: 1720 kB

#### 4.2.3 Tableau comparatif fork vs threads

| Critère             | fork()           | pthreads          |
|---------------------|------------------|-------------------|
| Mémoire au repos    | 1660 kB          | 1720 kB           |
| Mémoire sous charge | Elevée (copie)   | Faible (partagée) |
| Isolation           | Totale           | Partielle         |
| Communication IPC   | mmap/pipe requis | Variables directes|
| Complexité          | Faible           | Moyenne (mutex)   |
| Latence création    | Elevée (~1ms)    | Faible (~0.1ms)   |
| Sécurité            | Crash isolé      | Crash global      |

---

## 5. Multiplexage I/O avec select()

### 5.1 Principe

Un seul thread surveille plusieurs clients simultanément avec select(). Aucun
thread ni processus fils n'est créé. Un tableau clients[] de FD_SETSIZE
descripteurs est initialisé à -1. Timeout de 5 secondes. C'est la technique
utilisée par nginx et Redis.

### 5.2 Démonstration

#### 5.2.1 Test client silencieux

    # Terminal 1 — lancer le serveur
    $ ./tcp_server
    Serveur select démarré sur le port 9999
    Timeout, en attente...

    # Terminal 2 — client silencieux
    $ nc 127.0.0.1 9999
    (ne tape rien)

    # Terminal 3 — client actif
    $ nc 127.0.0.1 9999
    Bonjour
    Echo : Bonjour

    # Terminal 1 — affiche
    Descripteurs surveillés : 2

Le client silencieux ne bloque pas le client actif. Contrairement au modèle
itératif, un client inactif ne bloque pas les autres.

### 5.3 Réponses aux 4 questions select/poll/epoll

* select() est limité à FD_SETSIZE = 1024 descripteurs maximum. poll() utilise
un tableau dynamique sans limite fixe. 
* FD_SETSIZE=1024 est insuffisant en
production car un serveur peut recevoir des milliers de connexions simultanées.
* Pour 500 connexions poll() est préférable car son API est plus claire avec un
tableau de struct pollfd et le fd_set n'est pas à reconstruire à chaque appel.
* Pour 10 000+ connexions epoll (Linux) est recommandé avec une complexité O(1)
au lieu de O(n) pour select/poll. epoll est utilisé par nginx, Redis et Node.js
et supporte le mode edge-triggered pour encore plus de performance.

---

## 6. Daemon et syslog

### 6.1 Séquence de daemonisation

1. Premier fork : le père quitte, le fils continue
2. setsid() : crée une nouvelle session sans terminal de contrôle
3. Second fork : empêche toute réacquisition de terminal
4. chdir("/") : évite de bloquer un système de fichiers monté
5. umask(0) : contrôle total sur les permissions des fichiers créés
6. Redirection stdin/stdout/stderr vers /dev/null

### 6.2 Démonstration

#### 6.2.1 Lancer le daemon

    $ ./tcp_server
    (le terminal rend la main immédiatement)
    $ pgrep tcp_server
    6190

#### 6.2.2 Daemon survit au terminal

    (fermeture du terminal)
    $ pgrep tcp_server
    6190  (même PID, daemon toujours actif)

#### 6.2.3 Double instance refusée

    $ ./tcp_server
    (une seconde instance est refusée)

#### 6.2.4 Surveiller les logs en temps réel

    $ sudo tail -f /var/log/myserverd.log

    $ echo "Bonjour daemon" | nc 127.0.0.1 9999
    [Connexion #1] Echo : Bonjour daemon

    Logs observés dans /var/log/myserverd.log :
    myserverd[6190]: Daemon démarré sur le port 9999
    myserverd[6250]: Daemon déjà en cours (PID=6190)
    myserverd[6190]: Connexion acceptée de 127.0.0.1:54630
    myserverd[6304]: Client traité (fd=5)

    Niveaux syslog utilisés :
    LOG_INFO    : connexions normales
    LOG_WARNING : erreurs récupérables (accept échoué)
    LOG_ERR     : erreurs fatales (socket, bind, fork échoués)

    Configuration rsyslog ajoutée dans /etc/rsyslog.conf :
    daemon.*    /var/log/myserverd.log

---

## 7. Partie bonus — inetd

### 7.1 Rôle d'inetd

inetd est un super-daemon qui délègue la création de sockets au système plutôt
qu'aux applications. Au lieu que chaque service crée sa propre socket, inetd
écoute sur plusieurs ports et lance le programme correspondant à la demande.
L'avantage est qu'aucun processus serveur ne tourne en permanence — ils sont
lancés uniquement quand une connexion arrive, économisant ainsi les ressources
système.

### 7.2 Version inetd-compatible

La version inetd-compatible lit sur stdin et écrit sur stdout. inetd redirige
automatiquement vers la socket. Aucun bind(), listen() ni accept() n'est
nécessaire.

    /* echoserver_inetd.c */
    #include <stdio.h>
    #include <string.h>

    int main(void) {
        char buf[1024];
        ssize_t n;
        while ((n = read(0, buf, sizeof(buf))) > 0)
            write(1, buf, n);
        return 0;
    }

### 7.3 Ligne inetd.conf

    # port  type    protocol  wait    user    programme        arguments
    9998    stream  tcp       nowait  nobody  /chemin/echoserver  echoserver

### 7.4 Comparaison inetd vs daemon autonome

| Critère                      | inetd              | Daemon autonome     |
|------------------------------|--------------------|---------------------|
| Ressources à l'arrêt         | Aucune             | Mémoire permanente  |
| Latence au premier appel     | Elevée (fork/exec) | Faible (déjà actif) |
| Contrôle des connexions      | Limité             | Total               |
| Complexité du code serveur   | Très faible        | Moyenne             |

---

## 8. Conclusion

Pour un service en production, nous choisirions epoll avec un pool de threads
fixe comme le fait nginx. Le modèle fork pur est trop coûteux en mémoire sous
forte charge car chaque fils duplique toute la mémoire du père. Le modèle
select est limité à 1024 connexions. Le modèle threads pur sans pool peut
saturer le système. La combinaison epoll et thread pool offre une complexité
O(1) pour la surveillance des descripteurs, une mémoire partagée efficace
entre threads, un nombre de connexions simultanées illimité et une latence
minimale grâce au pool de threads préalloués.
