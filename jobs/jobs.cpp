#include "pch.h"

#include "Remotery/Remotery.h"
#include "jobs.h"
#include "semaphore.h"
#include "task.h"

#include <vector>
#include <windows.h>

using namespace std;


#define NUM_THREADS 3

#define MAKE_UPDATE_FUNC(NAME, DURATION)                                                           \
  void Update##NAME()                                                                              \
  {                                                                                                \
    rmt_ScopedCPUSample(NAME, 0);                                                                  \
    auto            start = chrono::high_resolution_clock::now();                                  \
    decltype(start) end;                                                                           \
    do                                                                                             \
    {                                                                                              \
      end = chrono::high_resolution_clock::now();                                                  \
    } while(chrono::duration_cast<chrono::microseconds>(end - start).count() < DURATION);          \
  }


MAKE_UPDATE_FUNC(Input, 2000)          // no dependencies
MAKE_UPDATE_FUNC(Physics, 10000)       // depends on Input
MAKE_UPDATE_FUNC(Collision, 12000)     // depends on Physics
MAKE_UPDATE_FUNC(Animation, 6000)      // depends on Collision
MAKE_UPDATE_FUNC(Particles, 8000)      // depends on Collision
MAKE_UPDATE_FUNC(GameElements, 24000)  // depends on Physics
MAKE_UPDATE_FUNC(Rendering, 20000)     // depends on Animation, Particles, GameElements
MAKE_UPDATE_FUNC(Sound, 10000)         // no dependencies


vector<Task*> g_TaskList;                     // contains all tasks that need to be executed
queue<Task*>  g_WorkQueue[NUM_THREADS];       // contains execution ready tasks for each thread
mutex         g_QueueMutex[NUM_THREADS];      // locks the queue when adding/removing elements
Semaphore     g_QueueSemaphore[NUM_THREADS];  // signals when items are added to the queue
Semaphore     g_ShedulerSemaphore;            // lets the sheduler wait until one task finishes
Semaphore     g_FinishUpdateSemaphore;        // used to wait for all tasks to finish


int main()
{
  Remotery*    remotery;
  atomic<bool> bIsRunning = true;
  thread       worker[NUM_THREADS];

  rmt_CreateGlobalInstance(&remotery);


  thread serial([&bIsRunning]() {
    while(bIsRunning)
      UpdateSerial();
  });


  for(size_t i = 0; i < NUM_THREADS; i++)
  {
    worker[i] = thread([&bIsRunning, i]() {
      while(bIsRunning)
      {
        if(g_QueueSemaphore[i].wait_for(0.5))
          ExecuteQueueTask(i);
        else
          StealQueueTask(i);
      }
    });
  }

  thread sheduler([&bIsRunning] {
    while(bIsRunning)
      UpdateParallel(&bIsRunning);
  });

  cout << "Type anything to quit...\n";
  char c;
  cin >> c;
  cout << "Quitting...\n";
  bIsRunning = false;

  for(size_t i = 0; i < NUM_THREADS; i++)
    worker[i].join();

  sheduler.join();
  serial.join();
  rmt_DestroyGlobalInstance(remotery);
}


void UpdateSerial()
{
  rmt_ScopedCPUSample(UpdateSerial, 0);
  UpdateInput();
  UpdatePhysics();
  UpdateCollision();
  UpdateAnimation();
  UpdateParticles();
  UpdateGameElements();
  UpdateRendering();
  UpdateSound();
}

void UpdateParallel(atomic<bool>* bIsRunning)
{
  rmt_ScopedCPUSample(UpdateParallel, 0);

  Task* InputTask        = new Task(UpdateInput, {});
  Task* SoundTask        = new Task(UpdateSound, {});
  Task* PhysicsTask      = new Task(UpdatePhysics, {InputTask});
  Task* CollisionTask    = new Task(UpdateCollision, {PhysicsTask});
  Task* GameElementsTask = new Task(UpdateGameElements, {PhysicsTask});
  Task* AnimationTask    = new Task(UpdateAnimation, {CollisionTask});
  Task* ParticlesTask    = new Task(UpdateParticles, {CollisionTask});
  Task* RenderingTask = new Task(UpdateRendering, {AnimationTask, ParticlesTask, GameElementsTask});
  Task* SyncTask      = new Task(FinishUpdate, {RenderingTask, SoundTask});

  g_TaskList.push_back(InputTask);
  g_TaskList.push_back(SoundTask);
  g_TaskList.push_back(PhysicsTask);
  g_TaskList.push_back(CollisionTask);
  g_TaskList.push_back(GameElementsTask);
  g_TaskList.push_back(AnimationTask);
  g_TaskList.push_back(ParticlesTask);
  g_TaskList.push_back(RenderingTask);
  g_TaskList.push_back(SyncTask);

  // continualy check for execution ready tasks to add to any work-queue
  while(g_TaskList.empty() == false)
  {
    for(size_t i = 0; i < NUM_THREADS; i++)
    {
      for(size_t j = 0; j < g_TaskList.size(); j++)
      {
        if(g_TaskList.at(j)->isExecutionReady())
        {
          g_QueueMutex[i].lock();
          g_WorkQueue[i].push(g_TaskList.at(j));
          g_TaskList.erase(g_TaskList.begin() + j);
          g_QueueMutex[i].unlock();
          g_QueueSemaphore[i].notify();
          break;
        }
      }
    }
    if(*bIsRunning == false)
      break;

    g_ShedulerSemaphore.wait();
  }

  if(*bIsRunning)
    g_FinishUpdateSemaphore.wait();

  for(size_t i = 0; i < NUM_THREADS; i++)
  {
    g_QueueSemaphore[i].reset();
    clear(g_WorkQueue[i]);
  }
  g_TaskList.clear();

  delete(InputTask);
  delete(SoundTask);
  delete(PhysicsTask);
  delete(CollisionTask);
  delete(GameElementsTask);
  delete(AnimationTask);
  delete(ParticlesTask);
  delete(RenderingTask);
  delete(SyncTask);
}

void ExecuteQueueTask(int nThreadID)
{
  Task* task = nullptr;

  g_QueueMutex[nThreadID].lock();
  task = g_WorkQueue[nThreadID].front();
  g_WorkQueue[nThreadID].pop();
  g_QueueMutex[nThreadID].unlock();

  if(task != nullptr)
  {
    task->executeTaskFunction();
    task->setFinished();
    g_ShedulerSemaphore.notify();
  }
}

void StealQueueTask(int nCurrentThreadID)
{
  for(size_t i = 0; i < NUM_THREADS; i++)
  {
    if(i != nCurrentThreadID && g_QueueSemaphore[i].try_take())
    {
      g_QueueMutex[i].lock();
      Task* stolenTask = g_WorkQueue[i].front();
      g_WorkQueue[i].pop();
      g_QueueMutex[i].unlock();

      g_QueueMutex[nCurrentThreadID].lock();
      g_WorkQueue[nCurrentThreadID].push(stolenTask);
      g_QueueMutex[nCurrentThreadID].unlock();

      g_QueueSemaphore[nCurrentThreadID].notify();
    }
  }
}


void clear(queue<Task*>& q)
{
  queue<Task*> empty;
  swap(q, empty);
}

void FinishUpdate()
{
  rmt_ScopedCPUSample(FinishUpdate, 0);
  g_FinishUpdateSemaphore.notify();
}
