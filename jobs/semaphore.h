#pragma once

#include <condition_variable>
#include <mutex>


class Semaphore
{
public:
  Semaphore(int count_ = 0)
      : m_nCount(count_)
  {
  }

  inline void notify()
  {
    std::unique_lock<std::mutex> lock(m_Mutex);
    ++m_nCount;
    m_ContitionVariable.notify_one();
  }

  inline bool wait_for(float timeout_ms)
  {
    using namespace std::chrono_literals;
    std::unique_lock<std::mutex> lock(m_Mutex);
    auto                         wakeupTime = std::chrono::system_clock::now() + timeout_ms * 1ms;

    if(m_ContitionVariable.wait_until(lock, wakeupTime, [this]() { return m_nCount == 0; }))
      return false;
    else
    {
      m_nCount--;
      return true;
    }
  }

  inline void wait()
  {
    std::unique_lock<std::mutex> lock(m_Mutex);
    while(m_nCount == 0)
    {
      m_ContitionVariable.wait(lock);
    }
    m_nCount--;
  }

  inline bool try_take()
  {
    std::unique_lock<std::mutex> lock(m_Mutex);
    if(m_nCount > 0)
    {
      m_nCount--;
      return true;
    }
    else
      return false;
  }

  inline void reset() { m_nCount = 0; }

private:
  std::mutex              m_Mutex;
  std::condition_variable m_ContitionVariable;
  std::atomic<int>        m_nCount;
};