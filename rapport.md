# Thread pool

Auteurs: Alex Berberat et Camille Koestli

## Description des fonctionnalités du logiciel

Le laboratoire implémente un thread pool qui permet de gérer un ensemble de threads pour exécuter des tâches de manière concurrente. Il gère un ensemble de threads de manière dynamique pour exécuter des tâches en parallèle, tout en respectant des contraintes comme des limites de threads actifs, une gestion de file d'attente et des timeouts. Les principales fonctionnalités sont:

- Gestion des tâches : Les tâches sont représentées par des objets de la classe Runnable, qui doivent implémenter les méthodes `run()`, `cancelRun()`, et `id()`.

- Limitation des ressources : Le nombre maximal de threads et la taille de la file d’attente des tâches sont contrôlés grâce à des paramètres `maxThreadCount` et `maxNbWaiting`.

- Timeouts pour les threads inactifs : Les threads sont terminés automatiquement après une période d’inactivité grâce à la variable `idleTimeout`.

- Master Thread : Le master du thread pool surveille l’état des threads et s'occupe de leur suppression s'il y a un timeout. Il garantit qu'aucune tâche n’est perdue ou interrompue.

## Choix d'implémentation

### Structure

L'implémentation de notre code utilise un système de thread pool dynamique, c'est-à-dire que les threads sont créés ou détruits en fonction de la charge de travail. Nous avons choisi de faire une implémentation sur le modèle avec un master thread qui gère les threads actifs et des threads qui exécutent les tâches récupérées dans la file d'attente.
Voici les classes principales, les sous-classes en fonction de leur rôle :

- `Runnable` : Définit une interface pour les tâches à exécuter.
- `ThreadPool` : Gère la création, la suppression, et l'exécution des threads.
- `Worker` : Structure représentant chaque thread, incluant son état et ses conditions de synchronisation. Lorsqu'une tâche est disponible, la condition associée au `Worker` est signalée, permettant au thread de récupérer la tâche. Après l'exécution d'une tâche, `isWorking` est remis à `false` et `previousTaskEnd` est mis à jour. Le thread master utilise `previousTaskEnd` pour supprimer les threads inactifs qui dépassent `idleTimeout`.

### Gestion du Threadpool

Le thread pool est géré par la classe `ThreadPool`. Cette classe est responsable de la gestion des threads, de la file d'attente des tâches, et de la synchronisation entre les threads.

- `void master_work();` : Fonction exécutée par le maître du thread pool. Elle surveille l'état des threads et supprime les threads inactifs.
- `void thread_work(size_t id);`: Fonction exécutée par chaque thread worker pour exécuter les tâches.
- `void start();` : Cette fonction est essentielle pour démarrer le thread pool. Elle permet d'ajouter des tâches `Runnable` à la file d'attente pour être exécutées par les threads.
  
### Synchronisation

- `taskQueue` : File d'attente des tâches.
- `workers` : Carte contenant l'état de chaque thread.
- Variables partagées comme `waitingThreads` et `activeWorkerCount`.

### Suppression des threads

Pour éviter les threads inutile, le master vérifie l'activité des threads. Les threads inactifs, après un certain temps `idleTimeout` sont supprimés, sauf si des tâches sont en attente.

### Arrêt du thread pool

Le thread pool utilise un destructeur `~ThreadPool` pour effectuer un arrêt. Cette méthode va :

1. L'arrêt du thread maître.
2. La demande d'arrêt de chaque thread ouvrier.
3. L'attente de la fin de l'exécution de tous les threads restants `join()`.
4. L'annulation des tâches restantes dans la file d'attente.

## Tests effectués

| Test   | Objectif                               | Résultat |
| ------ | -------------------------------------- | -------- |
| Test 1 | Vérification du fonctionnement de base | OK       |
| Test 2 | Gestion d'une surcharge de file        | OK       |
| Test 3 | Exécution par lots                     | OK       |
| Test 4 | Gestion des tâches refusées            | OK       |
| Test 5 | Timeout des threads inactifs           | OK       |

### Test 1 : Fonctionnement de base de base

L'objectif est la vérification de l'exécution correcte des tâches pour des tailles de pool variées.

### Test 2 : Gestion d'une surcharge de file

L'objectif est de vérifier que le pool de threads gère correctement les tâches en attente lorsque la file est pleine. Il simule des appels simultanés à la méthode `start()` depuis plusieurs threads pour vérifier l'absence de deadlocks.

### Test 3 : Exécution par lot de 10x10 tâches

L'objectif est de de valider l'exécution par lots successifs.

### Test 4 : Gestion des tâches refusées

L'objectif de ce test est de ester le comportement avec une file pleine.

### Test 5 : Timeout des threads inactifs

L'objectif est de vérifier que les threads inactifs sont supprimés après un certain temps.
