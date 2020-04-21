#pragma once

#include <vector>

class Task
{
public:
  Task() {}
  Task(void (*taskFunction)(void), std::initializer_list<Task*> children)
  {
    for(Task* child : children)
    {
      child->addParent(this);
      ++m_WorkingChildren;
    }
    m_TaskFunction = taskFunction;
  }
  ~Task() {}

  bool isExecutionReady() { return m_WorkingChildren == 0; }
  void executeTaskFunction() { m_TaskFunction(); }

  void notify()
  {
    if(m_WorkingChildren > 0)
      --m_WorkingChildren;
    else
      m_WorkingChildren = 0;
  }

  void setFinished()
  {
    rmt_ScopedCPUSample(FinishUpdate, 0);
    for(Task* parent : m_Parents)
    {
      parent->notify();
    }
  }

  void addParent(Task* task) { m_Parents.push_back(task); }

private:
  void (*m_TaskFunction)(void);
  std::vector<Task*> m_Parents;
  uint16_t           m_WorkingChildren = 0;
};