/**
 * 多生产者|单消费者
 * 
 * 
 * 1、生产者速率 > 消费者速率
 *  消费者只有一个，因此，不存在消费者之间的竞争。生产者存在多个，多个生产者之间生产好数据就需要按照
 *  竞争将数据放入缓冲区，谁先拿到锁，谁先放入。最开始，生产者多个，只能通过竞争生产，但是由于生产效
 *  率大于消费效率，所以定然会出现商品数量 > 消费者数量。当商品总量达到总数，则需要暂停生产，等待消费
 *  者消费
 * 
 * 2、生产者速率 < 消费者速率
 *  最开始，剩余放入总数  > 生产者总数，可以同时放入，随着时间的推移，可能会出现:剩余放入空间 > 生产者
 *  总数 和 剩余放入空间 < 生产者总数时，已经不满足同时放入，此时需要锁来保证
*/

#include <queue>
#include <mutex>
#include <vector>
#include <thread>
#include <iostream>
#include <condition_variable>

using namespace std;

//总生产数目
const int pro_cnt_total = 10;

//生产缓冲区大小
const int max_queue_size = 10;

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

    // 用于生产者之间的竞争
    std::mutex _mtx_pro;

    //计算当前已经生产了多少数据
    unsigned int _cnt_cur_pro = 0;

    repo_(const unsigned int max_queue_count = 10) : _max_queue_count(max_queue_count), _cnt_cur_pro(0){}

    repo_(const repo_ &instance) = delete;
    repo_& operator=(const repo_& instance) = delete;
    repo_(const repo_ &&instance) = delete;
    repo_& operator=(repo_&& instance) = delete;
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
    if(param_repo == nullptr || param_repo == NULL)
        return;
    

    bool is_running = true; 

    while (is_running)
    {
        {
            //用于生产者之间竞争
            std::unique_lock<std::mutex> lock(param_repo->_mtx_pro);

            //缓冲区没有满，继续生产
            if(param_repo->_cnt_cur_pro < pro_cnt_total)
            {
                thread_produce_item<T>(thread_index, *param_repo, param_repo->_cnt_cur_pro);
                ++param_repo->_cnt_cur_pro;
            }
            else
            {
                //生产者生产完成
                is_running = false;
            }
        }

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
    vec_thread.push_back(std::thread(thread_producer<int>, 1, pro_cnt_total, &repository));
    vec_thread.push_back(std::thread(thread_producer<int>, 2, pro_cnt_total, &repository));

    //消费者
    vec_thread.push_back(std::thread(thread_consumer<int>, 1, &repository));

    for(auto &item : vec_thread)
    {
        item.join();
    }

    return 0;

}
