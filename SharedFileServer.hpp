#ifndef __SHARED_FILE_SERVER_HPP__
#define __SHARED_FILE_SERVER_HPP__
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <strings.h>
#include <unistd.h>
#include "PoolThread.hpp"
#include "utils.hpp"

#define MAX_LISTEN 5
#define MAX_THREAD_NUM 3

class Sock
{
private:
    int _fd;
public:
    Sock():_fd(-1)
    {}
    ~Sock()
    {}
    bool Socket()
    {
        //非阻塞
        //_fd = socket(AF_INET,SOCK_NONBLOCK | SOCK_STREAM,0);
        _fd = socket(AF_INET,SOCK_STREAM,0);
        if(_fd < 0){
            //cerr << "socket error" << endl;
            LOG("sock error :%s\n",strerror(errno));
            return false;
        }
        int opt = 1;
        //处理TIME_WAIT状态,重启时会出现端口号被占用的情况,无法重启
        setsockopt(_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
        return true;
    }
    bool Bind(const uint16_t port)
    {
        struct sockaddr_in local;
        bzero(&local,sizeof(local));
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = INADDR_ANY;
        local.sin_port = htons(port);
        int ret = bind(_fd,(struct sockaddr*)&local,sizeof(local));
        if(ret < 0){
            cerr << "bind error!" << endl;
            LOG("bind error :%s\n",strerror(errno));
            return false;
        }
        return true;
    }
    bool Listen(int num)
    {
        int ret = listen(_fd,num);
        if(ret < 0){
            //cerr << "listen error!" << endl;
            LOG("listen error :%s\n",strerror(errno));
            return false;
        }
        return true;
    }
    int Accept(sockaddr_in* pclient = NULL,socklen_t* plen = NULL)
    {
        int new_sock = accept(_fd,(struct sockaddr*)pclient,plen);
        if(new_sock < 0){
            //cerr << "accept error!" << endl;
            LOG("accept error :%s\n",strerror(errno));
            return -1;
        }
        cout << "New Client Connect : [ip:]" << inet_ntoa(pclient->sin_addr)\
             << " [port]" << ntohs(pclient->sin_port) << endl;
        return new_sock;
    }
    bool Close()
    {
        close(_fd);
        return true;
    }
};

class HttpServer
{
private:
    ThreadPool* _pt;
    Sock _listen_sock;
    uint16_t _port;
public:
    HttpServer(const uint16_t port):_port(port){}
    bool InitServer(size_t num = MAX_LISTEN)
    {
        _pt = new ThreadPool(MAX_THREAD_NUM);
        _pt->ThreadPoolInit();
        return  _listen_sock.Socket() && \
        _listen_sock.Bind(_port) && \
        _listen_sock.Listen(num);
    }
    bool Start()
    {
        while(1){
            sockaddr_in client;
            socklen_t len = sizeof(client);
            int new_sock = _listen_sock.Accept(&client,&len);
            if(new_sock < 0){
                //cerr << "acept failure!" << endl;
                //LOG("accept error :%s\n",strerror(errno));
                continue;
            }
            Service(new_sock);
        }
        _listen_sock.Close();
        return true;
    }
    void Service(int new_sock)
    {
        HttpTask t(new_sock,handler);
        _pt->PushTask(t);
    }
    /////////////////////////////////////////////
    static bool handler(int new_sock)   //任务处理函数
    {
        RequestInfo info;                //解析后的请求信息
        HttpReQuest req(new_sock);       //请求信息
        HttpResponse rsp(new_sock);      //响应信息
        //接收请求头
        if(req.RecvHttpHeader(info) == false){
            std::cout<<"err in RecvHttpHeader\n";
            goto out;
        }
        //解析请求头
        if(req.ParseHttpHeader(info) == false){
            std::cout<<"err in ParseHttpHeader\n";
            goto out;
        }
        //判断请求类型
        if(info.RequestIsCGI()){
            rsp.CGIHandler(info);
        }
        else{
            rsp.FileHandler(info);
        }
        close(new_sock);
        return true;
     out:
        rsp.ErrHandler(info);
        close(new_sock);
        return true;
     }
};

#endif //__SHARED_FILE_SERVER_HPP__
