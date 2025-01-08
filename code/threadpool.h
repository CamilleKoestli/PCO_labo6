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
        bool isWorking;
        bool timedOut;
        std::chrono::milliseconds previousTaskEnd;
    };


    bool stop_requested;
    bool removingTimedOutThread;

    std::atomic<size_t> activeThreads;

    std::map<size_t, Worker> workers; //TODO: maybe replace with map not sure

    std::queue<std::unique_ptr<Runnable>> taskQueue;


    Condition removal_finished;
    Condition waiting_task;



    void master_work() {
        while (!PcoThread::thisThread()->stopRequested()) {

            //TODO: how to tell specific thread to stop
            //TODO: find way to calculat time for each

            monitorIn();

            removingTimedOutThread = true;

            for (auto &worker : workers) {
                if (!worker.second.isWorking) {
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()) - worker.second.previousTaskEnd >= idleTimeout) {

                        worker.second.timedOut = true;

                    }
                }
            }

            for (size_t i = 0; i < activeThreads; i++) {
                signal(waiting_task);
            }
            activeThreads--;

            removingTimedOutThread = false;
            signal(removal_finished);
            monitorOut();
        }
    }



    void thread_work(size_t id) {
        while (!PcoThread::thisThread()->stopRequested()) {

            monitorIn();

            while (taskQueue.empty() && !workers.at(id).timedOut && !PcoThread::thisThread()->stopRequested()) {
                wait(waiting_task);
            }


            workers.at(id).isWorking = true;

            if (PcoThread::thisThread()->stopRequested() || workers.at(id).timedOut) {
                monitorOut();
                return;
            }

            std::unique_ptr<Runnable> task = std::move(taskQueue.front());
            taskQueue.pop();

            monitorOut();


            task->run();


            monitorIn();
            workers.at(id).previousTaskEnd = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
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
        stop_requested(false),
        removingTimedOutThread(false),
        activeThreads(0) {

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


        //TODO: add missing things in destructor
        monitorIn();


        stop_requested = true;


        while (removingTimedOutThread) {
            wait(removal_finished);
        }


        ThreadPoolMaster.requestStop();

        while (!taskQueue.empty()) {
            taskQueue.front()->cancelRun();
            taskQueue.pop();
        }

        monitorOut();

        //TODO: opti maybe
        for (auto &worker : workers) {
            worker.second.thread->join();
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


        // TODO: verifiy if implementation is GorN

        monitorIn();

        if (taskQueue.size() >= maxNbWaiting) {
            runnable->cancelRun();
            monitorOut();
            return false;
        }

        if (activeThreads < maxThreadCount) {
            srand(time(NULL));
            size_t id;
            do {
                id = rand() % maxThreadCount;
            }
            while (workers.find(id) != workers.end());

            workers.emplace(id, Worker{ std::make_unique<PcoThread>(&ThreadPool::thread_work,this, id), false,  false,(std::chrono::milliseconds)0 });
            activeThreads++;
        }

        taskQueue.push(std::move(runnable));
        signal(waiting_task);

        monitorOut();

        return true;
    }


    /* Returns the number of currently running threads. They do not need to be executing a task,
     * just to be alive.
     */
    size_t currentNbThreads() {
        return activeThreads;
    }
};

#endif // THREADPOOL_H
