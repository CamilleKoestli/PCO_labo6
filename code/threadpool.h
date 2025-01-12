/**
 */

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <map>
#include <pcosynchro/pcohoaremonitor.h>
#include <pcosynchro/pcologger.h>
#include <pcosynchro/pcothread.h>
#include <queue>
#include <stack>
#include <stdlib.h>
#include <time.h>
#include <vector>

#define LOG_THREADS 1

class Runnable {
public:
    virtual ~Runnable() = default;

    /**
     * @brief Exécute la tâche.
     */
    virtual void run() = 0;

    /**
     * @brief Annule l'exécution de la tâche.
     */
    virtual void cancelRun() = 0;

    /**
     * @brief Retourne un identifiant unique pour la tâche.
     * @return L'identifiant de la tâche.
     */
    virtual std::string id() = 0;
};

/**
 * @brief Implémentation d'un thread pool avec suppression automatique des threads inactifs.
 */
class ThreadPool : public PcoHoareMonitor {
private:
    /**
     * @brief Structure représentant un thread worker dans le pool.
     */
    struct Worker {
        std::unique_ptr<PcoThread> thread;        // Thread associé au worker.
        std::unique_ptr<Condition> waiting_t;     // Condition associée au worker.
        bool isWorking = false;                   // Indique si le worker est en train de travailler.
        std::chrono::milliseconds previousTaskEnd;// Temps de fin de la dernière tâche.
    };

    size_t maxThreadCount;                // Nombre maximum de threads actifs.
    size_t maxNbWaiting;                  // Taille maximale de la file d'attente.
    std::chrono::milliseconds idleTimeout;// Temps d'inactivité avant suppression du thread.

    // Gestion des threads et des tâches
    PcoThread ThreadPoolMaster;              // Thread maître pour la gestion des threads workers.
    std::atomic<bool> removingTimedOutThread;// Indique si des threads sont en cours de suppression.
    std::atomic<size_t> waitingThreads;      // Nombre de threads en attente.
    std::atomic<size_t> activeWorkerCount;   // Nombre de threads actifs.

    std::map<size_t, Worker> workers;               // Map des threads workers.
    std::queue<std::unique_ptr<Runnable>> taskQueue;// File d'attente des tâches.

    Condition removal_finished;// Condition signalant la fin de la suppression des threads.


    /**
     * @brief Retourne l'heure actuelle en millisecondes.
     * @return Heure actuelle en millisecondes.
     */
    std::chrono::milliseconds getTime() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch());
    }

public:

    /**
     * @brief Fonction exécutée par le thread maître.
     * Gère la suppression des threads inactifs.
     */
    void master_work() {
        while (!PcoThread::thisThread()->stopRequested()) {
            monitorIn();

            removingTimedOutThread = true;

            std::chrono::milliseconds sleepTime(idleTimeout);
            const std::chrono::milliseconds gracePeriod(10);

            for (auto it = workers.begin(); it != workers.end();) {
                auto &worker = it->second;

                if (!worker.isWorking && (getTime() - worker.previousTaskEnd >= idleTimeout + gracePeriod)) {
                    if (waitingThreads > 0 && !taskQueue.empty()) {
                        ++it;
                        continue;
                    }

                    worker.thread->requestStop();
                    signal(*worker.waiting_t);
                    worker.thread->join();
                    it = workers.erase(it);

#if LOG_THREADS
                    logger() << "Thread supprimé pour timeout\n";
#endif
                } else {
                    ++it;
                }
            }

            removingTimedOutThread = false;
            signal(removal_finished);

            monitorOut();

            PcoThread::usleep(sleepTime.count());
        }
    }

    /**
     * @brief Fonction exécutée par chaque thread worker.
     * Gère la récupération et l'exécution des tâches.
     * @param id Identifiant du worker.
     */
    void thread_work(size_t id) {
        while (!PcoThread::thisThread()->stopRequested()) {
            monitorIn();

            while (taskQueue.empty() && !PcoThread::thisThread()->stopRequested()) {
                wait(*workers.at(id).waiting_t);
            }

            if (PcoThread::thisThread()->stopRequested()) {
                monitorOut();
                return;
            }

            activeWorkerCount++;
            workers.at(id).isWorking = true;

            auto task = std::move(taskQueue.front());
            taskQueue.pop();

            monitorOut();

            task->run();

            monitorIn();
            workers.at(id).previousTaskEnd = getTime();
            workers.at(id).isWorking = false;
            activeWorkerCount--;
            monitorOut();
        }
    }

public:

    /**
     * @brief Constructeur du thread pool.
     * @param maxThreadCount Nombre maximum de threads actifs.
     * @param maxNbWaiting Taille maximale de la file d'attente.
     * @param idleTimeout Temps d'inactivité avant suppression des threads.
     */
    ThreadPool(int maxThreadCount, int maxNbWaiting, std::chrono::milliseconds idleTimeout)
        : maxThreadCount(maxThreadCount),
          maxNbWaiting(maxNbWaiting),
          idleTimeout(idleTimeout),
          ThreadPoolMaster(&ThreadPool::master_work, this),
          removingTimedOutThread(false),
          waitingThreads(0),
          activeWorkerCount(0) {
        if (maxThreadCount < 1 || maxNbWaiting < 1 || idleTimeout.count() < 1) {
            throw std::invalid_argument("Invalid thread pool parameters");
        }
    }

    /**
     * @brief Destructeur du thread pool.
     * Termine proprement tous les threads et vide la file d'attente.
     */
    ~ThreadPool() {

        monitorIn();

        if (removingTimedOutThread) {
            wait(removal_finished);
        }

        ThreadPoolMaster.requestStop();
        ThreadPoolMaster.join();

        for (auto &worker: workers) {
            worker.second.thread->requestStop();
            signal(*worker.second.waiting_t);
        }

        monitorOut();

        for (auto &worker: workers) {
            worker.second.thread->join();
        }

        /* For some obscure reasons this cause a crash
        for (auto &worker : workers) {
            // Not sure about those 2
            worker.second.thread.reset();
            worker.second.waiting_t.reset();
            workers.erase(worker.first);
#if LOG_THREADS
            std::cout << "======= nbr threads : " << workers.size() << " =======" << std::endl;
#endif //LOG_THREADS
        }*/

        while (!taskQueue.empty()) {
            taskQueue.front()->cancelRun();
            taskQueue.pop();
        }
    }

    /**
     * @brief Ajoute une tâche à la file d'attente.
     * @param runnable Pointeur unique vers la tâche à ajouter.
     * @return `true` si la tâche a été ajoutée avec succès, `false` sinon.
     */
    bool start(std::unique_ptr<Runnable> runnable) {
        monitorIn();

        if (taskQueue.size() >= maxNbWaiting) {
            monitorOut();
            runnable->cancelRun();
            return false;
        }

        taskQueue.push(std::move(runnable));

        if (waitingThreads > 0) {
            for (auto &worker: workers) {
                if (!worker.second.isWorking) {
                    signal(*worker.second.waiting_t);
                    break;
                }
            }
        } else if (workers.size() < maxThreadCount) {
            size_t id = workers.size();
            workers.emplace(id, Worker{
                                        .thread = std::make_unique<PcoThread>(&ThreadPool::thread_work, this, id),
                                        .waiting_t = std::make_unique<Condition>(),
                                        .isWorking = false,
                                        .previousTaskEnd = getTime()});

#if LOG_THREADS
            logger() << "Thread créé: " << id << "\n";
#endif
        }

        monitorOut();
        return true;
    }

    /**
     * @brief Retourne le nombre actuel de threads actifs dans le pool.
     * @return Nombre de threads actifs.
     */
    size_t currentNbThreads() {
        monitorIn();
        size_t count = workers.size();
        monitorOut();
        return count;
    }
};

#endif// THREADPOOL_H
