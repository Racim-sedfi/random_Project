/**
 * @file client.c
 * @authors 
 *    Racim Sedfi
 *    Mohand-Said Mane
 * 
 * @brief Ce programme correspond au côté client qui calcule le nombre d'occurrences de valeurs aléatoires 
 * à l'aide de plusieurs processus. Les résultats sont ensuite envoyés à un serveur via une connexion TCP.
 * 
 * @version 1.1
 * @date 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <time.h>

#define RAND_TAB_TAILLE (1 << 20) /**< Taille du tableau random (2^20 = 1,048,576) */
#define NB_APPL_PROC 1000000000   /**< Nombre d'incrémentations aléatoires par processus */
#define NB_PROC 6                /**< Nombre de processus créés */

/* Variables globales */
unsigned int *tab_partager; /**< Tableau partagé en mémoire entre les processus */
sem_t *mutex, *barrier; /**< Sémaphores pour synchroniser les processus */

/**
 * @brief Affiche un message d'erreur et termine le programme.
 * 
 * @param msg Message d'erreur à afficher.
 */
void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

/**
 * @brief Initialise un tableau en remplissant toutes ses cases avec 0.
 * 
 * @param tab Pointeur vers le tableau.
 * @param taille Taille du tableau.
 */
void init_tab(unsigned int *tab, unsigned int taille) {
    for (unsigned int i = 0; i < taille; ++i) {
        tab[i] = 0;
    }  
}

/**
 * @brief Configure et initialise la mémoire partagée.
 */
void configurer_mem_partagee() {
    unsigned int shm_fd = shm_open("tab_partager", O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        error("Échec de shm_open");
    }
    ftruncate(shm_fd, RAND_TAB_TAILLE * sizeof(int));

    tab_partager = mmap(NULL, RAND_TAB_TAILLE * sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (tab_partager == MAP_FAILED) {
        close(shm_fd);
        error("Échec de mmap");
    }

    init_tab(tab_partager, RAND_TAB_TAILLE);
}

/**
 * @brief Configure et initialise les sémaphores nommés.
 */
void configurer_semaphores() {
    mutex = sem_open("/mutex", O_CREAT, 0644, 1);
    if (mutex == SEM_FAILED) {
        munmap(tab_partager, RAND_TAB_TAILLE * sizeof(unsigned int));
        error("Échec de sem_open pour mutex");
    }

    barrier = sem_open("/barrier", O_CREAT, 0644, 0);
    if (barrier == SEM_FAILED) {
        sem_close(mutex);
        sem_unlink("/mutex");
        munmap(tab_partager, RAND_TAB_TAILLE * sizeof(unsigned int));
        error("Échec de sem_open pour barrier");
    }
}

/**
 * @brief Génère des occurrences aléatoires et met à jour un tableau partagé.
 * 
 * @param id_proc Identifiant du processus (utilisé uniquement pour les journaux).
 */
void generer_tab_alea(unsigned int id_proc) {
    unsigned int tab_local[RAND_TAB_TAILLE];
    init_tab(tab_local, RAND_TAB_TAILLE);

    srand(getpid() + time(NULL));
    for (unsigned int i = 0; i < NB_APPL_PROC; ++i) {
        unsigned int valeur_alea = rand() % RAND_TAB_TAILLE;
        tab_local[valeur_alea]++;
    }

    // Ajouter les occurrences locales au tableau partagé
    sem_wait(mutex);
    for (unsigned int i = 0; i < RAND_TAB_TAILLE; i++) {
        tab_partager[i] += tab_local[i];
    }
    sem_post(mutex);

    printf("Processus %d a mis à jour le tableau partagé\n", id_proc);

    // Signal de synchronisation
    sem_post(barrier);
}

/**
 * @brief Envoie les données du tableau partagé au serveur.
 * 
 * Les données sont envoyées en une seule fois via une connexion TCP.
 * 
 * @param adresse_ip Adresse IP du serveur.
 * @param port Port du serveur.
 */
void envoyer_donnees_serveur(const char *adresse_ip,unsigned int port) {
    unsigned int sock_fd;
    struct sockaddr_in server_addr;

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        error("Échec de la création du socket");
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, adresse_ip, &server_addr.sin_addr);

    printf("Connexion au serveur %s:%d...\n", adresse_ip, port);

    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sock_fd);
        error("Échec de la connexion au serveur");
    }

    printf("Connexion établie avec le serveur\n");

    if (send(sock_fd, tab_partager, RAND_TAB_TAILLE * sizeof(unsigned int), 0) < 0) {
        close(sock_fd);
        error("Échec de l'envoi des données au serveur");
    }

    printf("Données envoyées au serveur\n");
    close(sock_fd);
}

/**
 * @brief Point d'entrée principal du programme client.
 * 
 * @param argc Nombre d'arguments.
 * @param argv Arguments (adresse IP et port du serveur).
 * @return int Code de retour du programme.
 */
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <adresse_ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *adresse_ip = argv[1];
    int port = atoi(argv[2]);

    configurer_mem_partagee();
    configurer_semaphores();

    // Forker les processus
    pid_t pids[NB_PROC];
    for (int i = 0; i < NB_PROC; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            error("Échec du fork");
        } else if (pids[i] == 0) {
            generer_tab_alea(i);
            exit(EXIT_SUCCESS);
        }
    }

    // Attendre la fin des processus enfants
    for (int i = 0; i < NB_PROC; i++) {
        sem_wait(barrier);
    }

    // Envoyer les données au serveur
    envoyer_donnees_serveur(adresse_ip, port);

    // Nettoyage
    munmap(tab_partager, RAND_TAB_TAILLE * sizeof(unsigned int));
    sem_close(mutex);
    sem_unlink("/mutex");
    sem_close(barrier);
    sem_unlink("/barrier");
    shm_unlink("tab_partager");

    return 0;
}
