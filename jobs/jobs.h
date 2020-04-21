#pragma once
#include <queue>

class Task;

void UpdateSerial();

void UpdateParallel(std::atomic<bool>* bIsRunning);

void ExecuteQueueTask(int nThreadID);

void StealQueueTask(int nCurrentThreadID);

void clear(std::queue<Task*>& q);

void FinishUpdate();
