# TCP Server UNIX

**Auteurs :**
- FIONONANA RANDRY Tino
- RAFANOMEZANTSOA Holinirina Vahatriniaina
- RAKOTOVAO Nantenaina Elvys

**Licence 3 Télécommunications**

---

## Partie 1 — Serveur TCP itératif

### Description
Serveur TCP minimaliste en mode itératif. Il traite un client à la fois.
La socket est créée avec socket(), configurée avec SO_REUSEADDR, bind() sur
le port 9999 et listen() avec un backlog de 10.

### Compilation et lancement
    make
    ./tcp_server

### Test 1 — Echo basique
    $ echo "Bonjour" | nc 127.0.0.1 9999
    [Connexion #1] Echo : Bonjour

### Test 2 — Blocage itératif
    Terminal 2 : nc 127.0.0.1 9999  (connecté mais silencieux)
    Terminal 3 : echo "Hello" | nc 127.0.0.1 9999  (bloqué, pas de réponse)
    Quand Terminal 2 fait Ctrl+C :
    Terminal 3 reçoit : [Connexion #2] Echo : Hello

### Analyse
Le serveur a une seule boucle accept/read/write. Tant qu'il est bloqué
sur read() du client 1, il ne revient pas à accept(). Le client 2 reste
dans la file d'attente du noyau (backlog).

---

## Partie 2 — Serveur concurrent avec fork()

### Description
Serveur concurrent basé sur fork(). Chaque connexion cliente est prise en
charge par un processus fils indépendant. Un gestionnaire SIGCHLD avec
waitpid(-1, NULL, WNOHANG) évite les processus zombies. Le compteur de
connexions actives est partagé via mmap MAP_SHARED | MAP_ANONYMOUS.

### Test fork — 8 clients simultanés
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

### Test pstree
    $ pstree -p $(pgrep tcp_server)
    tcp_server(9094)

### Test IPC mémoire
    $ ps -C tcp_server -o rss=
    1660 KB

### Test anti-zombies
    $ ps aux | grep Z
    Aucun zombie lié à tcp_server.
    Le gestionnaire SIGCHLD nettoie correctement les fils.

### Analyse
Les 8 clients sont servis en parallèle dans le désordre, ce qui prouve
le parallélisme. Le père reste en écoute. Les fils se terminent rapidement.
La solution IPC choisie est mmap car partagée avant fork(), plus rapide
qu'un fichier temporaire.

---

## Partie 3 — Serveur multi-threadé avec pthreads

### Description
Serveur concurrent basé sur pthreads. Chaque connexion est gérée par un
thread indépendant. Un mutex protège le compteur connexions_actives.
Pool de MAX_THREADS=16. pthread_detach() libère automatiquement les ressources.
Le descripteur connfd est passé via malloc() pour éviter toute race condition.

### Test threads — 8 clients + VmRSS
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

    $ cat /proc/$(pgrep tcp_server)/status | grep VmRSS
    VmRSS: 1720 kB

### Comparaison mémoire fork vs threads
    Version fork()   : 1660 kB
    Version threads  : 1720 kB
    Les threads partagent la mémoire, les processus la dupliquent.
    Sous forte charge, les threads sont plus efficaces.

### Test pool — 17 clients
    17 clients servis sans message "Serveur saturé" car les connexions
    sont trop courtes pour saturer le pool simultanément.

### Test race condition — 16 clients
    Compteur stable après 16 connexions simultanées.
    Le mutex protège correctement le compteur global.

---

## Partie 4 — Multiplexage I/O avec select()

### Description
Serveur mono-thread qui surveille plusieurs clients simultanément avec select().
Un tableau clients[] de FD_SETSIZE descripteurs initialisé à -1. Timeout de
5 secondes. Aucun thread ni processus fils créé.

### Test client silencieux
    Terminal 2 : nc 127.0.0.1 9999  (silencieux, ne tape rien)
    Terminal 3 : nc 127.0.0.1 9999
                 Bonjour
                 Echo : Bonjour
    Descripteurs surveillés : 2

### Analyse
Le client silencieux ne bloque pas le client actif. select() surveille
tous les descripteurs simultanément. Contrairement à la partie 1, un
client inactif ne bloque pas les autres.

### Réponses select vs poll
- select() est limité à FD_SETSIZE=1024 descripteurs, poll() n'a pas
  cette limite.
- FD_SETSIZE=1024 est insuffisant en production pour des milliers de
  connexions simultanées.
- Pour 500 connexions poll() est préférable car son API est plus claire
  et le fd_set n'est pas à reconstruire à chaque appel.
- Pour 10000+ connexions epoll (Linux) est recommandé avec O(1).

---

## Partie 5 — Daemon et syslog

### Description
Le serveur est transformé en daemon UNIX complet. Double fork + setsid()
détachent le serveur de tout terminal. Logs via syslog dans
/var/log/myserverd.log. Détection de double instance via /tmp/myserverd.pid.

### Séquence de daemonisation
1. Premier fork : le père quitte, le fils continue
2. setsid() : crée une nouvelle session sans terminal de contrôle
3. Second fork : empêche toute réacquisition de terminal
4. chdir("/") : évite de bloquer un système de fichiers monté
5. umask(0) : contrôle total sur les permissions
6. Redirection stdin/stdout/stderr vers /dev/null

### Test 1 — Daemon survit au terminal
    $ pgrep tcp_server
    6190
    (fermeture du terminal)
    $ pgrep tcp_server
    6190  (même PID, daemon toujours actif)

### Test 2 — Double instance refusée
    $ ./tcp_server
    Log : myserverd[6250]: Daemon déjà en cours (PID=6190)

### Test 3 — Syslog en temps réel
    $ echo "Bonjour daemon" | nc 127.0.0.1 9999
    [Connexion #1] Echo : Bonjour daemon

    Logs dans /var/log/myserverd.log :
    myserverd[6190]: Daemon démarré sur le port 9999
    myserverd[6190]: Connexion acceptée de 127.0.0.1:54630
    myserverd[6304]: Client traité (fd=5)
    myserverd[6250]: Daemon déjà en cours (PID=6190)

### Niveaux syslog utilisés
    LOG_INFO    : connexions normales
    LOG_WARNING : erreurs récupérables (accept échoué)
    LOG_ERR     : erreurs fatales (socket, bind, fork échoués)

### Configuration rsyslog
    Ligne ajoutée dans /etc/rsyslog.conf :
    daemon.*    /var/log/myserverd.log

---

## Tests finaux

### Valgrind — fuites mémoire
    $ valgrind --leak-check=full --track-origins=yes ./tcp_server
    definitely lost: 0 bytes
    possibly lost: 816 bytes (interne glibc pthread_create, pas notre code)
    Notre code ne présente aucune fuite mémoire.

### SO_REUSEADDR — redémarrage immédiat
    $ ./tcp_server  (Ctrl+C)
    $ ./tcp_server
    Serveur threads démarré sur le port 9999
    Pas de "Address already in use"

### Arrêt propre SIGINT
    Ctrl+C arrête proprement le serveur sur accept()
