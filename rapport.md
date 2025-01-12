# Thread pool

Auteurs: Camille Koestli et Alex Berberat

## Description des fonctionnalités du logiciel

Le programme implémente un thread pool qui permet de gérer un ensemble de threads pour exécuter des tâches de manière concurrente. Les principales fonctionnalités sont:

- Gestion des tâches : Les tâches sont représentées par des objets de la classe Runnable, qui doivent implémenter les méthodes `run()`, `cancelRun()`, et `id()`.

- Limitation des ressources : Le nombre maximal de threads et la taille de la file d’attente des tâches sont contrôlés grâce à des paramètres `maxThreadCount` et `maxNbWaiting`.

- Timeouts pour les threads inactifs : Les threads sont terminés automatiquement après une période d’inactivité grâce à la variable `idleTimeout`.

- Master Thread : Le master du thread pool surveille l’état des threads et s'occupe de leur suppression s'il y a un timeout. Il garantit qu'aucune tâche n’est perdue ou interrompue.

## Choix d'implémentation

### Approche

L'implémentation de notre code utilise un système de thread pool dynamique, c'est-à-dire que les threads sont créés ou détruits en fonction de la charge de travail.
Voici les classes principales, les sous-classes en fonction de leur rôle :

- `ThreadPool` : Gère la création, la suppression, et l'exécution des threads.
- `Runnable` : Définit une interface pour les tâches à exécuter.
- `Worker` : Structure représentant chaque thread, incluant son état et ses conditions de synchronisation.

### Méthodes

- `void master_work();` : Fonction exécutée par le maître du thread pool. Elle surveille l'état des threads et supprime les threads inactifs.
- `void thread_work(size_t id);`: Fonction exécutée par chaque thread worker pour exécuter les tâches.

### Synchronisation

- `taskQueue` : File d'attente des tâches.
- `workers` : Carte contenant l'état de chaque thread.
- Variables partagées comme `waitingThreads` et `activeWorkerCount`.

### Suppression des threads

Pour éviter les threads inutile, le master vérifie l'activité des threads. Les threads inactifs, après un certain temps (`idleTimeout`) sont supprimés, sauf si des tâches sont en attente.

## Tests effectués

| Test   | Objectif                               | Résultat |
| ------ | -------------------------------------- | -------- |
| Test 1 | Vérification du fonctionnement de base | OK       |
| Test 2 | Gestion d'une surcharge de file        | OK       |
| Test 3 | Exécution par lots                     | Echec    |
| Test 4 | Gestion des tâches refusées            | Parfois  |
| Test 5 | Timeout des threads inactifs           | OK       |

### Test 3 : Exécution par lot de 10x10 tâches

L'objectif est de de valider l'exécution par lots successifs.


### Test 4 : Gestion des tâches refusées

L'objectif de ce test est de ester le comportement avec une file pleine.