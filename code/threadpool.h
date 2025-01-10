#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <atomic>
#include <iostream>
#include <stack>
#include <vector>
#include <chrono>
#include <cassert>
#include <map>
#include <stdlib.h>
#include <time.h>
#include <pcosynchro/pcologger.h>
#include <pcosynchro/pcothread.h>
#include <pcosynchro/pcohoaremonitor.h>

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
        bool isWorking;
        std::chrono::milliseconds previousTaskEnd;
    };

    std::atomic<bool> removingTimedOutThread;
    std::atomic<size_t> waitingThreads;

    std::map<size_t, Worker> workers;
    std::queue<std::unique_ptr<Runnable>> taskQueue;

    Condition removal_finished;


    std::chrono::milliseconds getTime() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
    }


    void master_work() {
        while (!PcoThread::thisThread()->stopRequested()) {
            monitorIn();

            removingTimedOutThread = true;

            std::chrono::milliseconds sleepTime(getTime());

            for (auto &worker : workers) {

                if (!worker.second.isWorking && ((getTime() - worker.second.previousTaskEnd) >= idleTimeout)) {

                    worker.second.thread->requestStop();

                    signal(*worker.second.waiting_t);

                    worker.second.thread->join();

                    // Not sure about those 2
                    worker.second.thread.reset();
                    worker.second.waiting_t.reset();


                    workers.erase(worker.first);

#if LOG_THREADS
                    std::cout << "======= nbr threads : " << workers.size() << " =======" << std::endl;
#endif //LOG_THREADS

                } else if (!worker.second.isWorking && (sleepTime > (getTime() - worker.second.previousTaskEnd))) {
                    sleepTime = getTime() - worker.second.previousTaskEnd;
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

            waitingThreads++;

            while (taskQueue.empty() && !PcoThread::thisThread()->stopRequested()) {
                wait(*workers.at(id).waiting_t);
            }

            waitingThreads--;

            workers.at(id).isWorking = true;

            if (PcoThread::thisThread()->stopRequested()) {
                monitorOut();
                return;
            }

            std::unique_ptr<Runnable> task = std::move(taskQueue.front());
            taskQueue.pop();

            monitorOut();


            task->run();


            monitorIn();
            workers.at(id).previousTaskEnd = getTime();
            workers.at(id).isWorking = false;
            monitorOut();
        }
    }


public:
    ThreadPool(int maxThreadCount, int maxNbWaiting, std::chrono::milliseconds idleTimeout) :
        maxThreadCount(maxThreadCount),
        maxNbWaiting(maxNbWaiting),
        idleTimeout(idleTimeout),
        ThreadPoolMaster(&ThreadPool::master_work, this),
        removingTimedOutThread(false),
        waitingThreads(0) {

        //TODO:Verif if usefull
        if (maxThreadCount < 1) {
            throw std::invalid_argument("Can't have less than 1 thread.");
        }
        if (maxNbWaiting < 1) {
            throw std::invalid_argument("Can't have less than 1 task in queue.");
        }
        if (idleTimeout < (std::chrono::milliseconds)1) {
            throw std::invalid_argument("Can't have less than 1ms timeout.");
        }
    }


    ~ThreadPool() {
        // TODO : End smoothly

        if (removingTimedOutThread) {
            wait(removal_finished);
        }

        ThreadPoolMaster.requestStop();
        ThreadPoolMaster.join();

        monitorIn();

        for (auto &worker : workers) {
            worker.second.thread->requestStop();

            signal(*worker.second.waiting_t);
        }

        monitorOut();

        for (auto &worker : workers) {
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

        if (taskQueue.size() >= maxNbWaiting) {
            monitorOut();
            runnable->cancelRun();
            return false;
        }

        taskQueue.push(std::move(runnable));

        if (waitingThreads > taskQueue.size()) {
            for (auto &worker : workers) {
                if (!worker.second.isWorking) {
                    signal(*worker.second.waiting_t);
                    break;
                }
            }

        } else if (workers.size() < maxThreadCount) {
            srand(time(NULL));
            size_t id;
            do {
                id = rand() % maxThreadCount;
            }
            while (workers.find(id) != workers.end());

            workers.emplace(id, Worker{ .thread = std::make_unique<PcoThread>(&ThreadPool::thread_work,this, id),
                                        .waiting_t = std::make_unique<Condition>(),
                                        .isWorking = false,
                                        .previousTaskEnd = getTime() });

#if LOG_THREADS
            std::cout << "======= nbr threads : " << workers.size() << " =======" << std::endl;
#endif //LOG_THREADS

        }

        monitorOut();

        return true;
    }


    /* Returns the number of currently running threads. They do not need to be executing a task,
     * just to be alive.
     */
    size_t currentNbThreads() {
        return workers.size();
    }
};

#endif // THREADPOOL_H
