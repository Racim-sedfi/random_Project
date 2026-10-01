# Analyse de `rand()` en architecture client-serveur (C)

Programme client-serveur en C qui évalue l'uniformité du générateur pseudo-aléatoire
`rand()`. Chaque client lance plusieurs processus qui tirent un grand nombre de valeurs
aléatoires et comptent leurs occurrences dans un tableau partagé ; les tableaux sont
envoyés par **TCP** à un serveur qui les cumule, calcule des statistiques et sauvegarde
les résultats.

## Technologies

- C (Linux / POSIX)
- Sockets TCP
- Processus (`fork`), mémoire partagée POSIX (`shm_open`, `mmap`), sémaphores nommés

## Fonctionnalités principales

- **Client** (`client.c`) : 6 processus fils effectuent chacun 10⁹ tirages dans
  [0, 2²⁰[ ; les occurrences sont cumulées dans un tableau en mémoire partagée protégé par
  un sémaphore, puis le tableau complet est envoyé au serveur.
- **Serveur** (`serveur.c`) : attend le nombre de clients indiqué, cumule les tableaux reçus
  (accès protégé par sémaphores), écrit le résultat dans `resultats_final.txt` et affiche
  minimum, maximum, moyenne, variance et écart-type.
- **Analyse** : `analyse_histogramme.pdf` présente l'histogramme des occurrences
  (`histogramme.png`) et les statistiques obtenues (`statistique.png`).

## Structure du projet

```
├── client.c                 # Programme client
├── serveur.c                # Programme serveur
├── analyse_histogramme.pdf  # Rapport d'analyse des résultats
├── histogramme.png          # Histogramme des occurrences
└── statistique.png          # Statistiques obtenues
```

## Compilation et exécution

Prérequis : Linux et `gcc`.

```bash
gcc -o serveur serveur.c -lm -pthread
gcc -o client client.c -pthread

# Terminal 1 : serveur sur le port 8080, en attente d'1 client
./serveur 8080 1

# Terminal 2 : client connecté au serveur local
./client 127.0.0.1 8080
```

Le serveur génère `resultats_final.txt` (une ligne « valeur nombre_d'occurrences » par valeur
possible). Avec 10⁹ tirages par processus, l'exécution du client peut prendre plusieurs minutes.

## Auteurs

Racim Sedfi et Mohand-Said Mane — Université Le Havre Normandie, 2024/2025
