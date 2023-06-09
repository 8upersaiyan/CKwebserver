#ifndef NKWEBSERVER_THREADPOOL_H
#define NKWEBSERVER_THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"

// 线程池模板类, 代码复用
template<typename T>
class threadpool {
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int thread_number = 8, int max_requests = 10000);

    ~threadpool();

    bool append(T *request);

private:
    static void *worker(void *arg);

    void run();

private:
  
    int m_thread_number;// 线程池数量

    // 线程池数组, 大小与m_thread_number
    pthread_t *m_threads;

    // 请求队列中最多允许的, 等待处理的请求数量
    int m_max_requests;

    // 请求队列
    std::list<T *> m_workqueue;

    // 互斥锁
    locker m_queuelocker;

    // 信号量用来判断是否有任务需要处理
    sem m_queuestat;

    // 是否结束线程
    bool m_stop;
};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) :
        m_thread_number(thread_number), m_max_requests(max_requests),
        m_stop(false), m_threads(NULL) {

    if ((thread_number <= 0) || (max_requests <= 0)) {
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) {
        throw std::exception();
    }

    // 创建thread_number个线程, 并设置为线程分离
    for (int i = 0; i < thread_number; i++) {
        printf("create the %d th thread\n", i);

        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete [] m_threads;
            throw std::exception();
        }

        if (pthread_detach(m_threads[i])) {
            delete[]m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool() {
    delete[]m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T *request) {
    m_queuelocker.lock(); //上锁
    if (m_workqueue.size() > m_max_requests) {   //线程队列满了
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request); //增加一个线程
    m_queuelocker.unlock(); //解锁
    m_queuestat.post();  //信号量增加
    return true;
}

template<typename T>
void *threadpool<T>::worker(void *arg) {  //工作
    threadpool *pool = (threadpool *) arg; 
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run() {
    while (!m_stop) {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();     //为空解锁 
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();    //取出第一个
        m_queuelocker.unlock();
        if (!request) {
            continue;
        }
        request->process();  //任务运行
    }
}

#endif //NKWEBSERVER_THREADPOOL_H
