# 使用C++开发一个半同步半异步线程池

## 半同步半异步线程池介绍

在处理大量并发任务的时候，如果按照传统的方式，一个请求一个线程来处理请求任务，大量的线程创建和销毁将消耗过多的系统资源，还增加了线程上下文切换的开销，而通过线程池技术就可以很好地解决这些问题。线程池技术通过在系统中预先创建一定数量的线程，当任务请求到来时从线程池中分配一个预先创建的线程去处理任务，线程在处理完任务之后还可以重用，不会销毁，而是等待下次任务的到来。这样，通过线程池能避免大量的线程创建和销毁动作，从而节省系统资源，这样做的一个好处是，对于多核处理器，由于线程会被分配到多个CPU，会提高并行处理的效率。另一个好处是每个线程独立阻塞，可以防止主线程被阻塞而使主流程被阻塞，导致其他的请求得不到响应的问题。


半同步半异步线程池分成三层：
![ed6189b4a8e5b1b746668a8537974858.png](en-resource://database/2623:1)


第一层是同步服务层，它处理来自上层的任务请求，上层的请求可能是并发的，这些请求不是马上就会被处理，而是将这些任务放到一个同步排队层中，等待处理。

第二层是同步排队层，来自上层的任务请求都会加到排队层中等待处理。

第三层是异步服务层，这一层中会有多个线程同时处理排队层中的任务，异步服务层从同步排队层中取出任务并行的处理。


这种三层的结构可以最大程度处理上层的并发请求。对于上层来说只要将任务丢到同步队列中就行了，至于谁去处理，什么时候处理都不用关心，主线程也不会阻塞，还能继续发起新的请求。至于任务具体怎么处理，这些细节都是靠异步服务层的多线程异步并行来完成的，这些线程是一开始就创建的，不会因为大量的任务到来而创建新的线程，避免了频繁创建和销毁线程导致的系统开销，而且通过多核处理能大幅提高处理效率。

## 线程池实现的关键技术分析

线程池的基本概念和基本结构，它是由三层组成：同步服务层、排队层和异步服务层，其中排队层居于核心地位，因为上层会将任务加到排队层中，异步服务层同时也会取出任务，这里有一个同步的过程。在实现时，排队层就是一个同步队列，允许多个线程同时去添加或取出任务，并且要保证操作过程是安全的。线程池有两个活动过程，一个是往同步队列中添加任务的过程，另一个是从同步队列中取任务的过程
![1884a93e63b21ccdbdc93a215d2c8c44.png](en-resource://database/2625:1)

从活动图中可以看到线程池的活动过程，一开始线程池会启动一定数量的线程，这些线程属于异步层，主要用来并行处理排队层中的任务，如果排队层中的任务数为空，则这些线程等待任务的到来，如果发现排队层中有任务了，线程池则会从等待的这些线程中唤醒一个来处理新任务。同步服务层则会不断地将新的任务添加到同步排队层中，这里有个问题值得注意，有可能上层的任务非常多，而任务又是非常耗时的，这时，异步层中的线程处理不过来，则同步排队层中的任务会不断增加，如果同步排队层不加上限控制，则可能会导致排队层中的任务过多，内存暴涨的问题。因此，排队层需要加上限的控制，当排队层中的任务数达到上限时，就不让上层的任务添加进来，起到限制和保护的作用。

## 同步队列

同步队列即为线程中三层结构中的中间那一层，它的主要作用是保证队列中共享数据线程安全，还为上一层同步服务层提供添加新任务的接口，以及为下一层异步服务层提供取任务的接口。同时，还要限制任务数的上限，避免任务过多导致内存暴涨的问题。

同步队列的实现代码
```C++


#include<list>
#include<mutex>
#include<thread>
#include<condition_variable>
#include <iostream>
using namespace std;
template<typename T>
class SyncQueue
{
public:
    SyncQueue(int maxSize) :m_maxSize(maxSize), m_needStop(false)
    {
    }
    void Put(const T&x)
    {
        Add(x);
    }
    void Put(T&&x)
    {
        Add(std::forward<T>(x));
    }
    void Take(std::list<T>& list)
    {
        std::unique_lock<std::mutex> locker(m_mutex);
        m_notEmpty.wait(locker, [this]{return m_needStop || NotEmpty(); });
        if (m_needStop)
            return;
        list = std::move(m_queue);
        m_notFull.notify_one();
    }
    void Take(T& t)
    {
        std::unique_lock<std::mutex> locker(m_mutex);
        m_notEmpty.wait(locker, [this]{return m_needStop || NotEmpty(); });
        if (m_needStop)
            return;
        t = m_queue.front();
        m_queue.pop_front();
        m_notFull.notify_one();
    }
    
    void Stop()
    {
        {
            std::lock_guard<std::mutex> locker(m_mutex);
            m_needStop = true;
        }
        m_notFull.notify_all();
        m_notEmpty.notify_all();
    }
    bool Empty()
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        return m_queue.empty();
    }
    bool Full()
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        return m_queue.size() == m_maxSize;
    }
    size_t Size()
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        return m_queue.size();
    }
    int Count()
    {
        return m_queue.size();
    }
private:
    bool NotFull() const
    {
        bool full = m_queue.size() >= m_maxSize;
        if (full)
            cout << "缓冲区满了，需要等待..." << endl;
        return !full;
    }
    bool NotEmpty() const
    {
        bool empty = m_queue.empty();
        if (empty)
            cout << "缓冲区空了，需要等待...，异步层的线程ID: " << this_thread::get_id() << endl;
        return !empty;
    }
         
    template<typename F>
    void Add(F&&x)
    {
        std::unique_lock< std::mutex> locker(m_mutex);
        m_notFull.wait(locker, [this]{return m_needStop || NotFull(); });
        if (m_needStop)
            return;
        m_queue.push_back(std::forward<F>(x));
        m_notEmpty.notify_one();
    }
private:
    std::list<T> m_queue;                    // 缓冲区
    std::mutex m_mutex;                    // 互斥量和条件变量结合起来使用
    std::condition_variable m_notEmpty;    // 不为空的条件变量
    std::condition_variable m_notFull;     // 没有满的条件变量
    int m_maxSize;                         // 同步队列最大的size
    bool m_needStop;                       // 停止的标志
};


```

## 线程池

一个完整的线程池包括三层：同步服务层、排队层和异步服务层，其实这也是一种生产者—消费者模式，同步层是生产者，不断将新任务丢到排队层中，因此，线程池需要提供一个添加新任务的接口供生产者使用；消费者是异步层，具体是由线程池中预先创建的线程去处理排队层中的任务。排队层是一个同步队列，它内部保证了上下两层对共享数据的安全访问，同时还要保证队列不会被无限制地添加任务导致内存暴涨，这个同步队列将使用上一节中实现的线程池。另外，线程池还要提供一个停止的接口，让用户能够在需要的时候停止线程池的运行。

```C++

#include<list>
#include<thread>
#include<functional>
#include<memory>
#include <atomic>
#include "common/SyncQueue.hpp"
using namespace std;
const int MaxTaskCount = 100;
class ThreadPool
{
public:
    using Task = std::function<void()>;
    ThreadPool(int numThreads = std::thread::hardware_concurrency()):m_queue(MaxTaskCount)
    {
        //开启线程池
        Start(numThreads);
    }
    ~ThreadPool()
    {
        //如果没有停止则主动停止线程池
        Stop();
    }
    void Stop()
    {
        //保证多线程情况下只调用一次StopThreadGroup
        std::call_once(m_flag,[this](){StopThreadGroup();});
    }
    void AddTask(Task &&task)
    {
        m_queue.Put(std::forward<Task>(task));
    }
    void AddTask(Task &task)
    {
        m_queue.Put(task);
    }
private:
    void Start(int numThreads)
    {
        m_running = true;
        //创建线程数组
        for(int i = 0;i < numThreads; i++)
        {
            m_threadgroup.push_back(std::make_shared<std::thread>(&ThreadPool::RunInThread, this));
        }
    }
    void RunInThread()
    {
        while(m_running)
        {
            //取任务分别执行
            std::list<Task> list;
            m_queue.Take(list);
            for(auto& task : list)
            {
                if(!m_running)
                    return;
                task();
            }
        }
    }
    void StopThreadGroup()
    {
        m_queue.Stop();         //让同步队列中的线程停止
        m_running = false;      //置为false，让内部线程跳出循环并退出
        for(auto thread : m_threadgroup)
        {
            if(thread->joinable())
                thread->join();
        }
        m_threadgroup.clear();
    }
private:
    std::list<std::shared_ptr<std::thread>> m_threadgroup;      //处理任务的线程组
    SyncQueue<Task> m_queue;                                    //同步队列
    atomic_bool m_running;                                      //是否停止标识
    std::once_flag m_flag;                                      
};
```

ThreadPool有3个成员变量，一个是线程组，这个线程组中的线程是预先创建的，应该创建多少个线程由外面传入，一般建议创建CPU核数的线程以达到最优的效率，线程组循环从同步队列中取出任务并执行，如果线程池为空，线程组将处于等待状态，等待任务的到来。另一个成员变量是同步队列，它不仅用来做线程同步，还用来限制同步队列的上限，这个上限也是由使用者设置的。第三个成员变量是用来停止线程池的，为了保证线程安全，我们用到了原子变量atomic_bool。


线程池测试例子
```C++

void TestThdPool()
{
         ThreadPool pool;
         pool.Start(2);
         std::thread thd1([&pool]{
                  for (int i = 0; i < 10; i++)
                  {
                           auto thdId = this_thread::get_id();
                           pool.AddTask([thdId]{
                                    cout << "同步层线程

1的线程

ID: " << thdId << endl;
                           });
                  }
         });
         std::thread thd2([&pool]{
                  for (int i = 0; i < 10; i++)
                  {
                           auto thdId = this_thread::get_id();
                           pool.AddTask([thdId]{
                                    cout << "同步层线程

2的线程

ID: " << thdId << endl;
                             });
                  }
         });
         this_thread::sleep_for(std::chrono::seconds(2));
         getchar();
         pool.Stop();
         thd1.join();
         thd2.join();
}
```