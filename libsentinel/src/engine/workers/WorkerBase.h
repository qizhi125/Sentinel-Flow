#pragma once
#include <atomic>

class WorkerBase {
public:
    virtual ~WorkerBase() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void join() = 0;
    virtual bool isRunning() const = 0;
};