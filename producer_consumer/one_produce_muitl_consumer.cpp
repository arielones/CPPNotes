/**
 * 单一生产者|多消费者
 * 
 * 
 * 1、生产者速度 > 消费者速率
 *  最开始，生产者只有一个，生产一件商品放入缓冲区，但是此时存在多个消费者，处理方法和下面的情况一致。
 *  慢慢的，就会出现:商品数量 < 消费者数量两种情况出现。当出现商品数量 > 消费者数量时，需要保证多个消
 *  费者不能消费同一个数据，而且，既然有多个商品，那么，消费者之间消费无需等待
 * 
 * 2、生产者速率 < 消费者速率
 *  生产者只有一个，不存在生产之间的冲突；然而消费者存在多个，多个消费者之间存在竞争，既然是竞争，那就
 *  需要锁，哪个线程先拿到锁，就先消费；生产速率跟不上消费速率，因为有互斥锁，所以，消费者与生产者之间
 *  不存在冲突
*/


#include <queue>
#include <mutex>
#include <vector>
#include <thread>
#include <iostream>
#include <condition_variable>

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

    //用于消费者之间的竞争
    std::mutex _mtx_con;

    //计算当前已经消费了多少数据
    unsigned int _cnt_cur_con = 0;

    repo_(const unsigned int max_count_queue = 10):_max_queue_count(max_count_queue),_cnt_cur_con(0){}

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
        {
            // 保证多个消费者不消费同一个数据
            std::unique_lock<std::mutex> lock(param_repo->_mtx_con);

            //还没消费到指定的数目，继续消费
            if(param_repo->_cnt_cur_con < pro_cnt_total)
            {
                thread_consume_item<T>(thread_index, *param_repo);
                param_repo->_cnt_cur_con++;
            }
            else
            {
                //消费者消费完成
                is_run = false;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
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

    //消费者
    vec_thread.push_back(std::thread(thread_consumer<int>, 1, &repository));
    vec_thread.push_back(std::thread(thread_consumer<int>, 2, &repository));

    for(auto &item : vec_thread)
    {
        item.join();
    }

    return 0;
}


