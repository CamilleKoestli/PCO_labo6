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
    virtual void run() = 0;
    virtual void cancelRun() = 0;
    virtual std::string id() = 0;
};

class ThreadPool : public PcoHoareMonitor {
private:
    size_t maxThreadCount;
    size_t maxNbWaiting;
    std::chrono::milliseconds idleTimeout;

    PcoThread ThreadPoolMaster;

    struct Worker {
        std::unique_ptr<PcoThread> thread;
        std::unique_ptr<Condition> waiting_t;
        bool isWorking = false;
        std::chrono::milliseconds previousTaskEnd;
    };

    std::atomic<bool> removingTimedOutThread;
    std::atomic<size_t> waitingThreads;
    std::atomic<size_t> activeWorkerCount;

    std::map<size_t, Worker> workers;
    std::queue<std::unique_ptr<Runnable>> taskQueue;

    Condition removal_finished;


    std::chrono::milliseconds getTime() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch());
    }

    void master_work() {
        while (!PcoThread::thisThread()->stopRequested()) {
            monitorIn();

            removingTimedOutThread = true;

            std::chrono::milliseconds sleepTime(idleTimeout);
            const std::chrono::milliseconds gracePeriod(10);// 10 ms de grâce
            for (auto it = workers.begin(); it != workers.end();) {
                auto &worker = it->second;
                if (!worker.isWorking && (getTime() - worker.previousTaskEnd >= idleTimeout + gracePeriod)) {
                    // Vérifier qu'il n'y a pas de tâches en attente pour ce thread
                    if (waitingThreads > 0 && !taskQueue.empty()) {
                        ++it;
                        continue;
                    }
                    // Supprimer le thread
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


    ~ThreadPool() {
        // TODO : End smoothly

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


    /*
     * Start a runnable. If a thread in the pool is available, assign the
     * runnable to it. If no thread is available but the pool can grow, create a new
     * pool thread and assign the runnable to it. If no thread is available and the
     * pool is at max capacity and there are less than maxNbWaiting threads waiting,
     * block the caller until a thread becomes available again, and else do not run the runnable.
     * If the runnable has been started, returns true, and else (the last case), return false.
     */
    bool start(std::unique_ptr<Runnable> runnable) {
        monitorIn();


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


    /* Returns the number of currently running threads. They do not need to be executing a task,
     * just to be alive.
     */
    size_t currentNbThreads() {
        monitorIn();
        size_t count = workers.size();
        monitorOut();
        return count;
    }
};

#endif// THREADPOOL_H
