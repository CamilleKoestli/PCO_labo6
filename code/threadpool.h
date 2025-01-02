#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <pcosynchro/pcohoaremonitor.h>
#include <pcosynchro/pcologger.h>
#include <pcosynchro/pcothread.h>
#include <stack>
#include <vector>

class Runnable {
public:
    virtual ~Runnable() = default;
    virtual void run() = 0;
    virtual void cancelRun() = 0;
    virtual std::string id() = 0;
};


// Sous-classe qui encapsule la file d'attente des tâches
class TaskQueue : public PcoHoareMonitor {
public:
    TaskQueue(size_t maxSize) : maxSize(maxSize), nbWaiting(0), waitingSem(0) {}

    bool push(std::unique_ptr<Runnable> task) {
        monitorIn();
        if (queue.size() >= maxSize) {
            logger() << "TaskQueue: File d'attente pleine\n";
            monitorOut();
            return false;// File d'attente pleine
        }
        queue.push(std::move(task));
        logger() << "TaskQueue: Tâche push taille actuelle : " << queue.size() << "\n";
        if (nbWaiting > 0) {
            waitingSem.release();
        }
        monitorOut();
        return true;
    }

    std::unique_ptr<Runnable> pop(std::chrono::milliseconds timeout) {
        auto start = std::chrono::steady_clock::now();
        monitorIn();
        while (queue.empty()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);

            if (elapsed >= timeout) {
                logger() << "TaskQueue: Timeout atteint sans tâche\n";
                monitorOut();
                return nullptr;// Temps d'attente dépassé
            }

            nbWaiting++;
            logger() << "TaskQueue: File vide thread attend\n";
            monitorOut();
            waitingSem.acquire();
            monitorIn();
            nbWaiting--;
        }

        auto task = std::move(queue.front());
        queue.pop();
        logger() << "TaskQueue: Tâche pop taille restante : " << queue.size() << "\n";
        monitorOut();
        return task;
    }

    // Annuler toutes les tâches pas commencées
    void cancelAll() {
        monitorIn();
        while (!queue.empty()) {
            auto task = std::move(queue.front());
            queue.pop();
            task->cancelRun();
            logger() << "TaskQueue: Tâche annulée : " << task->id() << "\n";
        }
        monitorOut();
    }

    void notifyAll() {
        waitingSem.release();// Réveiller un thread en attente
    }


private:
    std::queue<std::unique_ptr<Runnable>> queue;// File d'attente tâches
    size_t maxSize;                             // Taille maximum de la file
    int nbWaiting;                              // Nombre de threads en attente
    PcoSemaphore waitingSem;                    // Sémaphore pour gérer l'attente
};


// Sous-classe qui gère un thread individuel du pool
class Worker {
public:
    Worker(TaskQueue &taskQueue, std::atomic<size_t> &activeCount, std::atomic<bool> &stopFlag)
        : taskQueue(taskQueue), activeCount(activeCount), shouldStop(stopFlag), thread(&Worker::run, this) {}

    ~Worker() {
        thread.join();
    }

private:
    void run() {
        while (true) {
            auto task = taskQueue.pop(std::chrono::milliseconds(100));
            if (shouldStop && !task) {
                // Si shouldStop est vrai et aucune tâche n'est récupérée, sortir
                break;
            }
            if (task) {
                ++activeCount;
                logger() << "Worker: Exécution de la tâche : " << task->id() << "\n";
                task->run();
                logger() << "Worker: Tâche terminée : " << task->id() << "\n";
                --activeCount;
            }
        }
        logger() << "Worker: Thread arrêté proprement.\n";
    }


    TaskQueue &taskQueue;            // File d'attente tâches
    std::atomic<size_t> &activeCount;// Compteur de tâches en cours
    std::atomic<bool> &shouldStop;   // Indicateur stop
    PcoThread thread;                // Thread
};


class ThreadPool {
public:
    ThreadPool(int maxThreadCount, int maxNbWaiting, std::chrono::milliseconds idleTimeout)
        : maxThreadCount(maxThreadCount), maxNbWaiting(maxNbWaiting), idleTimeout(idleTimeout),
          taskQueue(maxNbWaiting), activeThreads(0), stop(false) {
        for (int i = 0; i < maxThreadCount; ++i) {
            workers.emplace_back(std::make_unique<Worker>(taskQueue, activeThreads, stop));
            logger() << "ThreadPool: Thread créé : " << i << "\n";
        }
    }

    ~ThreadPool() {
        stop = true;// Signaler aux threads de s'arrêter

        // Relâcher les sémaphores pour réveiller tous les threads bloqués
        for (size_t i = 0; i < workers.size(); ++i) {
            taskQueue.notifyAll();// Ajoutez une méthode notifyAll dans TaskQueue
        }

        taskQueue.cancelAll();// Annuler toutes les tâches non commencées
        workers.clear();      // Attendre que tous les threads terminent proprement
        logger() << "ThreadPool: Pool de threads détruit.\n";
    }


    /*
     * Start a runnable. If a thread in the pool is available, assign the
     * runnable to it. If no thread is available but the pool can grow, create a new
     * pool thread and assign the runnable to it. If no thread is available and the
     * pool is at max capacity and there are less than maxNbWaiting threads waiting,
     * block the caller until a thread becomes available again, and else do not run the runnable.
     * If the runnable has been started, returns true, and else (the last case), return false.
     */
    bool start(std::unique_ptr<Runnable> runnable) {
        // TODO
        if (taskQueue.push(std::move(runnable))) {
            logger() << "ThreadPool: Tâche ajoutée au pool.\n";
            return true;
        } else {
            logger() << "ThreadPool: Tâche refusée.\n";
            return false;
        }
    }

    /* Returns the number of currently running threads. They do not need to be executing a task,
     * just to be alive.
     */
    size_t currentNbThreads() {
        // TODO
        return workers.size(); // Retourner le nombre total de threads dans le pool
    }

private:
    size_t maxThreadCount;                // Nombre maximum de threads
    size_t maxNbWaiting;                  // Nombre maximum de tâches en attente
    std::chrono::milliseconds idleTimeout;// Temps d'inactivité avant arrêt

    TaskQueue taskQueue;                         // File d'attente des tâches
    std::atomic<size_t> activeThreads;           // Nombre de tâches en cours
    std::atomic<bool> stop;                      // Indicateur d'arrêt
    std::vector<std::unique_ptr<Worker>> workers;// Liste des threads pool
};

#endif// THREADPOOL_H
