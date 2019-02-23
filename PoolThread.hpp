#pragma once 
#include <iostream>
#include <queue>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "utils.hpp"

typedef bool (*TaskHandler)(int);
class HttpTask
{
private:
    int _cli_sock;
    TaskHandler _handler;
public:
    HttpTask(int cli_sock = -1,TaskHandler handler = NULL)
    {
        _cli_sock = cli_sock;
        _handler = handler;

    }
    ~HttpTask()
    {}
    void Handler()
    {
        _handler(_cli_sock);
    }
};

class ThreadPool
{
private:
    int _thread_num;  //最大线程池
    int _cur_num;     //当前线程池的线程数
    std::queue<HttpTask> _task_queue;
    pthread_mutex_t _mutex;
    pthread_cond_t _cond;
    bool _is_stop;    //线程池是否终止
public:
    ThreadPool(int max = 5):_thread_num(max),_cur_num(0),_is_stop(false)
    {}
    ~ThreadPool()
    {
        pthread_mutex_destroy(&_mutex);
        pthread_cond_destroy(&_cond);
    }
    static void*  PthreadRoutine(void* arg)
    {
        ThreadPool* tp = (ThreadPool*)arg;
        pthread_detach(pthread_self());
        for(; ;){
            tp->LockQueueTask();
            while(tp->TaskQueueIsEmpty()){
                tp->ThreadWait();
            }
            HttpTask t;
            tp->PopTask(t);
            tp -> UnLockQueueTask();
            t.Handler();
        }
    }
    bool ThreadPoolInit()
    {
        pthread_mutex_init(&_mutex,NULL);                 
        pthread_cond_init(&_cond,NULL);
        pthread_t tid[_thread_num];
        for(int i=0; i<_thread_num; i++){
            int ret = pthread_create(&tid[i],NULL,PthreadRoutine,this);
            if(ret != 0){
                LOG("thread create error\n");
                return false;
            }
            _cur_num++;
        }
        return true;
    }
    void PushTask(const HttpTask& t)
    {
        LockQueueTask();
        _task_queue.push(t);
        UnLockQueueTask();
        WakeOneThread();
    }
    void PopTask(HttpTask& t)       //线程出队之前就会进行加锁
    {
        t = _task_queue.front();
        _task_queue.pop();
    }
    void Stop()
    {
        LockQueueTask();            //防止此时又添加对象
        _is_stop = true;
        UnLockQueueTask();
        while(_cur_num > 0){
            WakeAllThread();
        }
    }
private:
    void ThreadWait()
    {
        if(_is_stop){           //若线程池要终止,则直接解锁退出
            UnLockQueueTask();
            _cur_num--;
            pthread_exit(NULL);
        }
        pthread_cond_wait(&_cond,&_mutex);
    }
    bool TaskQueueIsEmpty()
    {
        return _task_queue.empty();
    }
    void LockQueueTask()
    {
        pthread_mutex_lock(&_mutex);
    }
    void UnLockQueueTask()
    {
        pthread_mutex_unlock(&_mutex);
    }
    void WakeOneThread()
    {
        pthread_cond_signal(&_cond);
    }
    void WakeAllThread()
    {
        pthread_cond_broadcast(&_cond);
    }
};
