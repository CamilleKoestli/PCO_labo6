#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <atomic>
#include <iostream>
#include <stack>
#include <vector>
#include <chrono>
#include <cassert>
#include <pcosynchro/pcologger.h>
#include <pcosynchro/pcothread.h>
#include <pcosynchro/pcohoaremonitor.h>

class Runnable {
public:
    virtual ~Runnable() = default;
    virtual void run() = 0;
    virtual void cancelRun() = 0;
    virtual std::string id() = 0;
};


// sous-classe qui encapsule la file d'attente des tâches
class TaskQueue : public PcoHoareMonitor {
public:
    TaskQueue(size_t maxSize) : maxSize(maxSize), nbWaiting(0), waitingSem(0) {}

    bool push(std::unique_ptr<Runnable> task) {
         monitorIn();
        if (queue.size() >= maxSize) {
            monitorOut();
            return false; // File d'attente pleine.
        }
        queue.push(std::move(task));
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
                monitorOut();
                return nullptr; // Temps d'attente dépassé.
            }

            nbWaiting++;
            monitorOut();
            waitingSem.acquire();
            monitorIn();
            nbWaiting--;
        }

        auto task = std::move(queue.front());
        queue.pop();
        monitorOut();
        return task;
    }


private:
    std::queue<std::unique_ptr<Runnable>> queue;
    size_t maxSize;
    int nbWaiting;
    PcoSemaphore waitingSem;
};


// sous-classe qui gère un thread individuel du pool
class Worker {
public:
    Worker(TaskQueue& taskQueue, std::atomic<size_t>& activeCount)
        : taskQueue(taskQueue),  activeCount(activeCount), thread(&Worker::run, this) {}

    ~Worker() {
        thread.join();
    }

private:
    void run() {
       
    }

    TaskQueue& taskQueue;
    std::atomic<size_t>& activeCount;
    PcoThread thread;
};


class ThreadPool {
public:
    ThreadPool(int maxThreadCount, int maxNbWaiting, std::chrono::milliseconds idleTimeout)
        : maxThreadCount(maxThreadCount), maxNbWaiting(maxNbWaiting), idleTimeout(idleTimeout),taskQueue(maxNbWaiting), activeThreads(0) {
            for (int i = 0; i < maxThreadCount; ++i) {
            workers.emplace_back(std::make_unique<Worker>(taskQueue, activeThreads));
        }
    }

    ~ThreadPool() {
        // TODO : End smoothly
        
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
        return taskQueue.push(std::move(runnable));
    }

    /* Returns the number of currently running threads. They do not need to be executing a task,
     * just to be alive.
     */
    size_t currentNbThreads() {
        // TODO
    }

private:

    size_t maxThreadCount;
    size_t maxNbWaiting;
    std::chrono::milliseconds idleTimeout;

    TaskQueue taskQueue;
    std::atomic<size_t> activeThreads;
    std::vector<std::unique_ptr<Worker>> workers;
};

#endif // THREADPOOL_H
