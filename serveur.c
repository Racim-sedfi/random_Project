 /**
 * @file serveur.c
 * @authors 
 *    Racim Sedfi
 *    Mohand-Said Mane
 * 
 * @brief Implémentation du serveur pour traiter les données envoyées par les clients.
 * Le serveur utilise un tableau partagé pour stocker les données cumulées des clients et
 * synchronise l'accès à ce tableau à l'aide de plusieurs sémaphores. Les résultats sont ensuite sauvegardés
 * dans un fichier texte et visualisés à l'aide d'un graphe.
 * 
 * @version 1.1
 * @date 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define RAND_TAB_TAILLE (1 << 20) /**< Taille du tableau random (2^20 = 1,048,576) */

/* Variables globales */
int *tab_partager; /**< Tableau général partagé entre les processus */
sem_t *mutex1, *mutex2; /**< Sémaphores pour synchronisation et protection */
int clients_traites = 0; /**< Compteur des clients déjà traités */
int nb_clients;          /**< Nombre de clients à attendre */
int shm_fd;              /**< Descripteur de la mémoire partagée */

/**
 * @brief Initialise un tableau à 0.
 * 
 * @param tab Pointeur vers le tableau à initialiser.
 * @param taille Taille du tableau.
 */
void init_tab(int *tab, int taille) {
    for (int i = 0; i < taille; ++i) {
        tab[i] = 0;
    }
}

/**
 * @brief Génère un tableau local avec des valeurs aléatoires et ajoute les résultats au tableau partagé.
 */
void generer_tab_alea() {
    int tab_local[RAND_TAB_TAILLE];
    init_tab(tab_local, RAND_TAB_TAILLE);

    srand(time(NULL) + getpid());
    for (int i = 0; i < RAND_TAB_TAILLE; ++i) {
        int valeur_alea = rand() % RAND_TAB_TAILLE;
        tab_local[valeur_alea]++;
    }

    sem_wait(mutex1);
    for (int i = 0; i < RAND_TAB_TAILLE; i++) {
        tab_partager[i] += tab_local[i];
    }
    sem_post(mutex1);
}

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
 * @brief Sauvegarde les résultats dans un fichier texte.
 */
void sauvegarder_resultats() {
    FILE *fp = fopen("resultats_final.txt", "w");
    if (fp == NULL) {
        perror("Impossible d'ouvrir le fichier pour sauvegarde");
        return;
    }

    for (int i = 0; i < RAND_TAB_TAILLE; i++) {
        fprintf(fp, " %d %d\n",i , tab_partager[i]);
    }

    fclose(fp);
    printf("Résultats sauvegardés dans resultats_final.txt\n");
}

/**
 * @brief Affiche les statistiques du tableau partagé.
 */
void afficher_statistiques() {
    sem_wait(mutex2);
    int min = tab_partager[0], max = tab_partager[0];
    long long somme = 0;
    for (int i = 0; i < RAND_TAB_TAILLE; i++) {
        if (tab_partager[i] < min) min = tab_partager[i];
        if (tab_partager[i] > max) max = tab_partager[i];
        somme += tab_partager[i];
    }
    sem_post(mutex2);

    double moyenne = (double)somme / RAND_TAB_TAILLE;
    double variance = 0.0;
    sem_wait(mutex2);
    for (int i = 0; i < RAND_TAB_TAILLE; i++) {
        variance += pow(tab_partager[i] - moyenne, 2);
    }
    sem_post(mutex2);
    variance /= RAND_TAB_TAILLE;

    printf("Statistiques:\n");
    printf("Min: %d\n", min);
    printf("Max: %d\n", max);
    printf("Moyenne: %.2f\n", moyenne);
    printf("Variance: %.2f\n", variance);
    printf("Ecart-type: %.2f\n", sqrt(variance));
}

/**
 * @brief Gère la connexion et les données envoyées par un client.
 * 
 * @param client_sock Descripteur de socket du client.
 */
void gerer_client(int client_sock) {
    int *client_data = malloc(RAND_TAB_TAILLE * sizeof(int));
    if (client_data == NULL) {
        perror("Erreur d'allocation");
        close(client_sock);
        return;
    }

ssize_t bytes_read, total_read = 0;
while (total_read < RAND_TAB_TAILLE * sizeof(int)) {
    bytes_read = read(client_sock, (char *)client_data + total_read, 
                      (RAND_TAB_TAILLE * sizeof(int)) - total_read);
    if (bytes_read <= 0) {
        perror("Erreur ou déconnexion pendant la lecture des données");
        free(client_data);
        close(client_sock);
        return;
    }
    total_read += bytes_read;
}

if (total_read < RAND_TAB_TAILLE * sizeof(int)) {
    perror("Données incomplètes reçues");
    free(client_data);
    close(client_sock);
    return;
}


    printf("Reçu %zd octets du client (socket fd: %d)\n", bytes_read, client_sock);

    sem_wait(mutex1);
    for (int i = 0; i < RAND_TAB_TAILLE; i++) {
        tab_partager[i] += client_data[i];
    }
    clients_traites++;
    sem_post(mutex1);

    free(client_data);
    close(client_sock);

    if (clients_traites >= nb_clients) {
        printf("Tous les clients ont été traités.\n");
        generer_tab_alea();
        sauvegarder_resultats();
        afficher_statistiques();
        exit(EXIT_SUCCESS);
    }
}

/**
 * @brief Configure la mémoire partagée pour le tableau.
 */
void configurer_mem_partagee() {
    shm_fd = shm_open("tab_partager", O_CREAT | O_RDWR, 0666);
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
 * @brief Configure les sémaphores pour synchroniser l'accès au tableau.
 */
void configurer_semaphores() {
    mutex1 = sem_open("/mutex1", O_CREAT, 0666, 1);
    if (mutex1 == SEM_FAILED) {
        error("Échec de sem_open pour mutex1");
    }

    mutex2 = sem_open("/mutex2", O_CREAT, 0666, 1);
    if (mutex2 == SEM_FAILED) {
        sem_close(mutex1);
        sem_unlink("/mutex1");
        error("Échec de sem_open pour mutex2");
    }
 
}

/**
 * @brief Libère les sémaphores à la fin de l'exécution.
 */
void nettoyer_semaphores() {
    sem_close(mutex1);
    sem_unlink("/mutex1");
 
}

/**
 * @brief Point d'entrée principal du serveur.
 * 
 * @param argc Nombre d'arguments.
 * @param argv Arguments (port et nombre de clients).
 * @return int Code de retour.
 */
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <nb_clients>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    nb_clients = atoi(argv[2]);

    if (nb_clients <= 0) {
        fprintf(stderr, "Le nombre de clients doit être un entier positif.\n");
        exit(EXIT_FAILURE);
    }

    configurer_mem_partagee();
    configurer_semaphores();

    generer_tab_alea(); /**< Créer le tableau d'occurrences au côté serveur. */

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        error("Échec de la création du socket");
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sock_fd);
        error("Échec du bind");
    }

    if (listen(sock_fd, nb_clients) < 0) {
        close(sock_fd);
        error("Échec de listen");
    }

    printf("Le serveur écoute sur le port %d\n", port);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (clients_traites < nb_clients) {
        int client_sock = accept(sock_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("Échec d'acceptation du client");
            continue;
        }

        printf("Client connecté (socket fd: %d)\n", client_sock);
        gerer_client(client_sock);
    }

    close(sock_fd);
    nettoyer_semaphores();
    munmap(tab_partager, RAND_TAB_TAILLE * sizeof(int));
    shm_unlink("tab_partager");

    return 0;
}
