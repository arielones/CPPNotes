#include <list>
#include <thread>
#include <memory>
#include <atomic>
#include <future>
#include <iostream>
#include <functional>
#include "common/SyncQueue.hpp"
using namespace std;

const int MaxTaskCount = 100;

class ThreadPool
{
public:
    using Task = std::function<void()>;
    ThreadPool(int numThreads = std::thread::hardware_concurrency()):m_queue(MaxTaskCount)
    {
        //开启线程池
        Start(numThreads);
    }
    ~ThreadPool()
    {
        //如果没有停止则主动停止线程池
        Stop();
    }

    void Stop()
    {
        //保证多线程情况下只调用一次StopThreadGroup
        std::call_once(m_flag,[this](){StopThreadGroup();});
    }

    void AddTask(Task &&task)
    {
        m_queue.Put(std::forward<Task>(task));
    }

    void AddTask(Task &task)
    {
        m_queue.Put(task);
    }

    //提交要由线程池异步执行的函数
    template <typename F,typename... Args>
    auto SubmitFuture(F&& f,Args&&... args) -> std::future<decltype(f(args...))>
    {
        //创建一个绑定参数的函数用于执行
        std::function<decltype(f(args..))())> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);    //连接函数和参数定义，特殊函数类型，避免左右值错误

        //将其封装到共享指针中，以便能够复制构造
        auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);

        //将任务打包转换为void function
        Task warpper_func = [task_ptr]()
        {
            (*task_ptr)();
        }

        //压入安全队列
        AddTask(std::move(warpper_func));

        //返回先前注册的任务指针
        return task_ptr->get_future();
    }

    template<class F, class... Args>
    void SubmitVoid(F&& f, Args&&... args)
    {
        //创建一个绑定参数的函数用于执行
        std::function<Task> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);    //连接函数和参数定义，特殊函数类型，避免左右值错误

        //将其封装到共享指针中，以便能够复制构造
        auto task_ptr = std::make_shared<Task>(func);

        //将任务打包转换为void function
        Task warpper_func = [task_ptr]()
        {
            (*task_ptr)();
        }

        //压入安全队列
        AddTask(std::move(func));
    }

private:
    void Start(int numThreads)
    {
        m_running = true;
        //创建线程数组
        for(int i = 0;i < numThreads; i++)
        {
            m_threadgroup.push_back(std::make_shared<std::thread>(&ThreadPool::RunInThread, this));
        }
    }

    void RunInThread()
    {
        while(m_running)
        {
            //取任务分别执行
            std::list<Task> list;
            m_queue.Take(list);
            for(auto& task : list)
            {
                if(!m_running)
                    return;
                task();
            }
        }
    }

    void StopThreadGroup()
    {
        m_queue.Stop();         //让同步队列中的线程停止
        m_running = false;      //置为false，让内部线程跳出循环并退出
        for(auto thread : m_threadgroup)
        {
            if(thread->joinable())
                thread->join();
        }

        m_threadgroup.clear();

    }
private:
    std::list<std::shared_ptr<std::thread>> m_threadgroup;      //处理任务的线程组
    SyncQueue<Task> m_queue;                                    //同步队列
    atomic_bool m_running;                                      //是否停止标识
    std::once_flag m_flag;                                      
};