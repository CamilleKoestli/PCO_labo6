/**
 * Gestion d'un pool de threads pour exécuter des tâches concurrentes avec suppression des threads inactifs.
 * 
 * Ce fichier contient l'implémentation de la classe `ThreadPool` permettant de gérer efficacement un ensemble de threads pour l'exécution de tâches en parallèle.
 * 
 * @authors : Alex Berberat et Camille Koestli
 * 
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

#define LOG 1

#if LOG
#define LOG_THREADS 1
#define LOG_TASKS 1
#endif

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

    PcoThread ThreadPoolMaster;// Thread maître pour la gestion des threads workers.
    std::atomic<size_t> waitingThreads;   // Nombre de threads en attente.
    std::atomic<size_t> activeWorkerCount;// Nombre de threads actifs.

    std::map<size_t, Worker> workers;               // Map des threads workers.
    std::queue<std::unique_ptr<Runnable>> taskQueue;// File d'attente des tâches.

    Condition removal_finished;// Condition signalant la fin de la suppression des threads.
    Condition maxWait; // Condition signalant que la file d'attente est pleine.

    size_t waitingTasks = 0; // Nombre de tâches en attente.

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

#if LOG
        std::stringstream master_work_logger;
#endif

        while (!PcoThread::thisThread()->stopRequested()) {
            monitorIn();

            std::chrono::milliseconds sleepTime(idleTimeout);
            const std::chrono::milliseconds gracePeriod(10);

            for (auto it = workers.begin(); it != workers.end();) {
                auto &worker = it->second;
                auto tmp = it->first;

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
                    master_work_logger
                            << "===== [master_work]\n"
                            << "         [Thread] TimedOut: " << tmp << "\n"
                            << "         [ThreadPool] Size: " << workers.size() << "\n";
#endif

                } else {
                    ++it;
                }
            }

#if LOG
            logger() << master_work_logger.str();
            master_work_logger.flush();
#endif

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

#if LOG
        std::stringstream thread_work_logger;
#endif

        while (!PcoThread::thisThread()->stopRequested()) {
            monitorIn();

            waitingThreads++;
            while (taskQueue.empty() && !PcoThread::thisThread()->stopRequested()) {
                wait(*workers.at(id).waiting_t);
            }
            waitingThreads--;

            if (PcoThread::thisThread()->stopRequested()) {
                monitorOut();
                return;
            }

            activeWorkerCount++;
            workers.at(id).isWorking = true;

#if LOG_TASKS
            thread_work_logger
                    << "===== [thread_work]\n"
                    << "         [Task] Thread: " << id << " -> " << taskQueue.front()->id() << "\n"
                    << "         [TaskQueue] Size: " << taskQueue.size() << "\n";
#endif

            auto task = std::move(taskQueue.front());
            taskQueue.pop();


#if LOG
            logger() << thread_work_logger.str();
            thread_work_logger.flush();
#endif

            monitorOut();

            task->run();

            monitorIn();
            workers.at(id).previousTaskEnd = getTime();
            workers.at(id).isWorking = false;
            activeWorkerCount--;

            if (waitingTasks > 0) {
                signal(maxWait);
            }

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
#if LOG
        logger() << "===== [~ThreadPool] Called\n";
#endif

        monitorIn();

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

#if LOG
        std::stringstream start_logger;
#endif

        monitorIn();

#if LOG_TASKS
        start_logger
                << "===== [start]\n"
                << "        [Task] New: " << runnable->id() << "\n"
                << "        [TaskQueue] Size: " << taskQueue.size() << "\n";
#endif

        if (waitingThreads > 0) {
            taskQueue.push(std::move(runnable));

            for (auto &worker: workers) {
                if (!worker.second.isWorking) {
                    signal(*worker.second.waiting_t);
                    break;
                }
            }


        } else if (workers.size() < maxThreadCount) {
            taskQueue.push(std::move(runnable));
            size_t id = workers.size();
            workers.emplace(id, Worker{
                                        .thread = std::make_unique<PcoThread>(&ThreadPool::thread_work, this, id),
                                        .waiting_t = std::make_unique<Condition>(),
                                        .isWorking = false,
                                        .previousTaskEnd = getTime()});

#if LOG_THREADS
            start_logger << "        [Thread] Created id: " << id << "\n";
#endif

        } else if (waitingTasks < maxNbWaiting) {
            waitingTasks++;
            wait(maxWait);
            waitingTasks--;
            taskQueue.push(std::move(runnable));
        } else {
            monitorOut();
            runnable->cancelRun();
            return false;
        }


#if LOG
        logger() << start_logger.str();
        start_logger.flush();
#endif

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
