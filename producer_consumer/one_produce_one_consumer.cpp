#include <queue>
#include <mutex>
#include <vector>
#include <thread>
#include <iostream>
#include <condition_variable>

using namespace std;

template<typename T>
struct repo_
{
    //用作互斥访问缓冲区
    std::mutex _mtx_queue;

    //缓冲区最大size
    unsigned int _max_queue_count = 10;

    //缓冲区
    std::queue<T> _queue;

    //缓冲区没有满，通知生产者继续生产
    std::condition_variable _cv_queue_not_full;

    //缓冲区不为空，通知消费者继续消费
    std::condition_variable _cv_queue_not_empty;

    repo_(const unsigned int max_queue_count = 10) : _max_queue_count(max_queue_count){}
};


template<typename T>
using repo = repo_<T>;


//-----------------------------------------------------------------

//生产者生产数据
template<typename T>
void thread_produce_item(const int &thread_index, repo<T>& param_repo, const T& repo_item)
{
    std::unique_lock<std::mutex> lock(param_repo._mtx_queue);

    //1.生产者只要发现缓冲区没有满，就继续生产
    param_repo._cv_queue_not_full.wait(lock, [&]{return
    param_repo._queue.size() < param_repo._max_queue_count;});

    //2.将生产好的商品放入缓冲区
    param_repo._queue.push(repo_item);

    //log to console
    std::cout << "Producer:" << thread_index << "produce:" << repo_item << std::endl;

    //3.通知消费者可以消费了
    param_repo._cv_queue_not_empty.notify_one();
}

//-----------------------------------------------------------------
//消费者消费数据
template<typename T>
T thread_consume_item(const int thread_index, repo<T>& param_repo)
{
    std::unique_lock<std::mutex> lock(param_repo._mtx_queue);

    //1.消费者需要等待[缓冲区不为空]的信号
    param_repo._cv_queue_not_empty.wait(lock,[&]{return !param_repo._queue.empty();});

    //2.取出数据
    T item;
    item = param_repo._queue.front();
    param_repo._queue.pop();

    std::cout << "Consumer:" << thread_index << "consume:" << item << std::endl;

    //3.通知生成者，继续生产
    param_repo._cv_queue_not_full.notify_one();

    return item;
}

//-----------------------------------------------------------------
/**
* @ brief: 生产者线程
* @ thread_index - 线程标识，区分是哪一个线程
* @ count_max_produce - 最大生产次数
* @ param_repo - 缓冲区
* @ return - void  
*/

template<typename T>
void thread_producer(const int thread_index, const int max_count_produce, repo<T>* param_repo)
{
    for(int item = 0; item < max_count_produce; ++item)
    {
        thread_produce_item<T>(thread_index, *param_repo, item);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

/**
* @ brief: 消费者线程
* @ thread_index - 线程标识，区分线程
* @ param_repo - 缓冲区
* @ return - void      
*/
template<typename T>
void thread_consumer(const int thread_index, repo<T>* param_repo)
{
    bool is_run = true;
    while(is_run)
    {
        T item;
        item = thread_consume_item<T>(thread_index, *param_repo);

        std::this_thread::sleep_for(std::chrono::seconds(1));

        if(param_repo->_max_queue_count - 1 == item)
            is_run = false;
    }
}


//-----------------------------------------------------------------
//入口函数
int main(int ahrc,char *argc[], char *enc[])
{
    //缓冲区
    repo<int> repository;

    //线程池
    std::vector<std::thread> vec_thread;

    //生产者
    vec_thread.push_back(std::thread(thread_producer<int>, 1, 10, &repository));

    //消费者
    vec_thread.push_back(std::thread(thread_consumer<int>, 1, &repository));

    for(auto &item : vec_thread)
    {
        item.join();
    }

    return 0;

}


