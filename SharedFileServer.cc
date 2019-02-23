#include <stdlib.h>
#include <signal.h>
#include "SharedFileServer.hpp"
int main(int argc,char* argv[])
{
    if(argc != 2){
        cerr << "arguement error!" << endl;
        exit(1);
    }
    //忽略SIGPIPE信号，防止客户端关闭连接，发送失败收到该信号而导致进程终止
    signal(SIGPIPE,SIG_IGN);
    HttpServer* pserver = new HttpServer(atoi(argv[1]));
    if(pserver -> InitServer()){
        pserver -> Start();
    }
    return 0;
}
