#ifndef __UTILS_HPP__
#define __UTILS_HPP__
#include <iostream>
#include <sstream>
#include <string>
#include <string.h>
#include <unordered_map>
#include <vector>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/wait.h>

#define WWWROOT "www"
#define MAX_HTTPDR 4096
#define MAX_PATH 256       //文件最大路径
#define MAX_BUFF 4096
#define LOG(...) do{fprintf(stdout,__VA_ARGS__);fflush(stdout);}while(0)

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;
//错误码对应错误信息
std::unordered_map<string,string> g_err_desc = {
    {"206","Partial Content"},
    {"400","Bad Request"},
    {"403","Forbidden"},
    {"404","Not Found"},
    {"405","Method Not Allowed"},
    {"413","Request Entity Too Large"},
    {"500","Internal Server Error"}
};
//文件后缀对应标签
std::unordered_map<string,string> g_mime_type = {
    {"txt","text/plain"},
    {"html","text/html"},
    {"htm","text/html"},
    {"jpg","image/jpeg"},
    {"zip","application/zip"},
    {"mp3","audio/mpeg"},
    {"mpeg","video/mpeg"},
    {"unknow","application/octet-stream"},
    {"ico","image/x-icon"},
    {"png","image/png"}
};
struct RequestInfo
{
//包含HttpRequest解析出的请求信息
    string _method;       //请求方法
    string _version;      //协议版本
    string _path_info;    //资源路径
    string _path_phys;    //请求资源的实际路径
    string _query_string; //查询字符串,就是要查询的字段
    string _error_msg;    //错误信息
    string _fsize;        //文件大小
    std::unordered_map<string,string> _hdr_list;   //整个头部信息中的键值对
    struct stat _st;      //获取文件信息，包含文件类型

    bool RequestIsCGI()    //判断该请求是否为CGI请求
    {
        if(_method == "POST" || (_method == "GET" && ! _query_string.empty()))
            return true;
        else 
            return false;
    }
};

class Utils
{
public:
    static void GetMime(const string& file,string& mime)
    {
        size_t pos;
        pos = file.rfind('.');
        if(pos == string::npos){
            mime = g_mime_type["unknow"];
            return;
        }
        string suffix = file.substr(pos+1);
        auto it = g_mime_type.find(suffix);
        if(it == g_mime_type.end()){
            mime = g_mime_type["unknow"];
            return;
        }
        mime = g_mime_type[suffix];
    }
    static void DigitToStr(int64_t num,string& str)
    {
        std::stringstream ss;
        ss << num;
        str = ss.str();
    }
    static int64_t StrToDigit(const string& str)
    {
        //long long int 8字节,long int 8字节
        int64_t num;
        num = strtol(str.c_str(),NULL,10);
        return num;
    }
    static void MakeETag(int64_t size,int64_t ino,string& mtime,string& etag)
    {
        std::stringstream ss;
        ss << "\"";
        ss << std::hex << size;
        ss << "-";
        ss << std::hex << ino;
        ss << "-";
        ss << std::hex << mtime;
        ss << "\"";
        etag = ss.str();
    }
    static string GetErrDesc(string& err_msg)
    {
        auto it = g_err_desc.find(err_msg);
        if ( it != g_err_desc.end()){
            return it->second; 
        }
        return "UnKnow Error";
    }
    //切割头部信息
    static int Split(string& src,const string& seg,vector<string>& list)
    {
        size_t idx = 0;
        size_t pos = 0;
        while(idx < src.length()){
            pos = src.find(seg,idx);
            if(pos == string::npos){
                break;
            }
            list.push_back(src.substr(idx,pos-idx));
            idx = pos + seg.length();
        }
        if(idx < src.length()){
            list.push_back(src.substr(idx));
        }
        return list.size();
    }
    static void TimeToGMT(time_t& t,string& gmt)
    {
        //把时间放到tm结构体中
        struct tm* mt = gmtime(&t);
        char tmp[128] = {0};
        //%a 星期 %d 日 %b 月 %Y 年 %H 时 %M 分 %S 秒
        //把时间转成字符串
        int len = strftime(tmp,127,"%a, %d %b %Y %H:%M:%S GMT",mt);
        gmt.assign(tmp,len);
    }
};

//接收信息
class HttpReQuest
{
private:
    int _cli_sock;
    string _http_header;  //头部
public:
    HttpReQuest(int cli_sock):_cli_sock(cli_sock){}
    bool RecvHttpHeader(RequestInfo& info)  //接收http请求头
    {
        while(1){
            char buf[MAX_HTTPDR] = {0};
            //将_cli_sock设成非阻塞式读
            //int old_potion = fcntl(_cli_sock,F_GETFL);
            //fcntl(_cli_sock,F_SETFL,old_potion | O_NONBLOCK);

            //从接受缓冲区中读数据,但接受缓冲区的数据还在
            int ret = recv(_cli_sock,buf,MAX_HTTPDR,MSG_PEEK);
            if(ret < 0){
                //EINTR读的时候被信号打断，EAGIN读的时候缓冲区没数据(非阻塞)
                if(errno == EINTR || errno == EAGAIN)
                    continue;
                info._error_msg = "500";
                return false;
            }
            else if(ret == 0){
                info._error_msg = "404";
                return false;
            }
            //查看buf中是否有\r\n\r\n
            char* pos = strstr(buf,"\r\n\r\n");  
            //缓冲区都读完了,头还没有读完
            if((pos == NULL) && (ret == MAX_HTTPDR)){
                info._error_msg = "413";
                return false;
            }
            //头信息没有读完
            else if(pos == NULL && ret < MAX_HTTPDR){
                continue;
            }
            //头部长度
            int handler_len = pos - buf;
            _http_header.assign(buf,handler_len);  //读handler_len个字节,没有读\r\n\r\n
            cout << _http_header << endl;
            //从缓冲区再读一次,把头和空行都读进来,剩下的数据就是正文了
            recv(_cli_sock,buf,handler_len+4,0);
            return true;
        }
    }
    //解析首行
    bool PareseFirstLine(vector<string>& line_list,RequestInfo& info)
    {
        info._method = line_list[0];
        string& url = line_list[1];
        info._version = line_list[2];
        if(info._method != "GET" && info._method != "POST" && info._method != "HEAD"){
            info._error_msg = "405";
            return false;
        }
        if(info._version != "HTTP/0.9" && info._version != "HTTP/1.0" && info._version != "HTTP/1.1"){
            info._error_msg = "400";
            return false;
        }
        //url : /upload?key=val&key=val
        size_t pos;
        pos = url.find('?');
        //url ?之前是文件路径，之后是查询字符串
        if(pos == string::npos){
            info._path_info = url;
        }
        else{
            info._path_info = url.substr(0,pos);     //文件路径
            info._query_string = url.substr(pos+1);  //查询字符串 
        }
        return true;
    }
    //解析请求头
    bool ParseHttpHeader(RequestInfo& info)  
    {
        //方法 URL 协议版本\r\n
        //key: val\r\nkey: val
        vector<string> hdr_list;
        //先按行裁断
        Utils::Split(_http_header,"\r\n",hdr_list);
        //再裁首行
        vector<string> firstline_list;
        if(Utils::Split(hdr_list[0]," ",firstline_list) != 3){
            info._error_msg = "400";
            return false;
        }
        if(PareseFirstLine(firstline_list,info) == false){
            return false;
        }
        hdr_list.erase(hdr_list.begin());   //删首行
        for(size_t i=0;i<hdr_list.size();i++){
            size_t pos = hdr_list[i].find(": ");
            info._hdr_list[hdr_list[i].substr(0,pos)] = hdr_list[i].substr(pos+2);
        }       
        return PathIsLegal(info._path_info,info);
    }

    bool PathIsLegal(string& path,RequestInfo& info)
    {
        //www为根目录
        string file = WWWROOT + path;
        //返回文件信息，在一个stat结构体中
        if(stat(file.c_str(),&info._st) < 0){
            info._error_msg = "404";
            return false;
        }
        char tmp[MAX_PATH] = {0};
        //realpath函数，将相对路径转化为绝对路径,一旦不存在绝对路径，将会发生段错误
        realpath(file.c_str(),tmp);
        //设置文件真实路径，并将文件大小转换成字符串形式   
        info._path_phys = tmp;
        Utils::DigitToStr(info._st.st_size,info._fsize); 

        if(info._path_phys.find(WWWROOT) == string::npos){
            info._error_msg = "403";
            return false;
        }
        return true;
    }
};

class HttpResponse
{
private:
    int _cli_sock;
    //客户端如果请求相同的文件，且服务器端对该文件没有任何修改，就不用再次返回该文件了
    //ETag:"inode-fsize-mtime"可判断是否修改过该文件
    string _etag;		//查看文件是否被修改过
    string _mtime;		//文件最后一次修改时间
    string _date;               //系统的响应时间
public:
    HttpResponse(int cli_sock):_cli_sock(cli_sock){}
    bool InitResponse(RequestInfo& req_info)  //初始化一些请求的响应信息
    {
        //Last-Modified;
        int64_t size = req_info._st.st_size;
        int64_t ino = req_info._st.st_ino;
        
        //把文件最后修改时间弄成GMT时间
        Utils::TimeToGMT(req_info._st.st_mtime,_mtime);
        
        //ETag,将文件大小，inode号，文件最后一次修改时间弄在一起放入Etag中
        Utils::MakeETag(size, ino, _mtime, _etag);
        
        //Date
        time_t t = time(NULL); 
        Utils::TimeToGMT(t,_date);
        return true;
    }
    bool FileIsDir(RequestInfo& info)
    {
        if(info._st.st_mode & S_IFDIR){
            if(info._path_info.back() != '/'){
                info._path_info.push_back('/');
            }
            if(info._path_phys.back() != '/'){
                info._path_phys.push_back('/');
            }
            return true;
        }
        return false;
    }
    bool ProcessFile(RequestInfo& info) //文件下载
    {
        //组织html头部
        string rsp_header = info._version + " 200 OK\r\n";
        string mime;
        Utils::GetMime(info._path_info,mime);
        rsp_header += "Content-Type: application/octet-stream;charset=UTF-8\r\n"; 
        rsp_header += "Etag: " + _etag + "\r\n";
        int64_t len = info._st.st_size;
        string slen;
        Utils::DigitToStr(len,slen);
        rsp_header += "Content-Length: " + slen + "\r\n"; 
        rsp_header += "Accept-Ranges: bytes\r\n";
        rsp_header += "Date: " + _date + "\r\n\r\n";
        cout << rsp_header << endl;
        SendData(rsp_header);
        //发送文件
        int fd = open(info._path_phys.c_str(),O_RDONLY);
        if(fd < 0){
            info._error_msg = "400";
            return false;
        }
        char tmp[MAX_BUFF];
        int rlen;
        while((rlen = read(fd,tmp,MAX_BUFF))>0){
            send(_cli_sock,tmp,rlen,0);
        }
        close(fd);
        return true;
    }
    bool ProcessList(RequestInfo& info) //文件列表功能
    {
        //组织头部：
        //首行
        //content-Type: text/html\r\n
        //ETag: 
        //Date: 
        //Connection: close\r\n\r\n
        //Transfer-Encoding: chunked\r\n
        //正文：
        //   每一个文件都要组织一个html标签的信息
        string rsp_header = info._version + " 200 OK\r\n";
        rsp_header += "Content-Type: text/html;charset=UTF-8\r\n"; 
        rsp_header += "Etag: " + _etag + "\r\n";
        rsp_header += "Date: " + _date + "\r\n";
        //if(info._version == "HTTP/1.1")
        //    rsp_header += "Transfer-Encoding: chunked\r\n";
        rsp_header += "Connextion: close\r\n\r\n";
        cout << rsp_header;
        SendData(rsp_header);   //头部正常传输
        
        string rsp_body;
        //这是网页上的一个标题
        rsp_body = "<html><head><title>Index of " + info._path_info + "</title>";
        //html中的信息
        rsp_body += "<body><h1>Index of " + info._path_info + "</h1>";
        rsp_body += "<from>";
        //enctype 属性规定在发送到服务器之前应该如何对表单数据进行编码。
        rsp_body += "<form action='/upload' method='POST' enctype='multipart/form-data'>";
        rsp_body += "<input type='file' name='UpLoadFile' value='abc' />";
        //定义上传按钮
        rsp_body += "<input type='submit' value='上传' />";
        rsp_body += "</form>";
        rsp_body += "<meta charset='UTF-8'></head>";
        rsp_body += "<hr /><ol>";
        SendData(rsp_body);
        //获取目录下的每一个文件、组织处理html信息
        struct dirent** p_dirent = NULL;
        //以alphasort为排序方法
        int num = scandir(info._path_phys.c_str(),&p_dirent,0,alphasort);
        for(int i=0; i<num; i++){
            string name = p_dirent[i]->d_name;
            string file_html = "<li>";
            string file = info._path_phys + name;
            struct stat st;
            if(stat(file.c_str(),&st)<0)
                continue;
            string mtime;
            string mime;
            string fsize;
            Utils::TimeToGMT(st.st_mtime,mtime);
            Utils::GetMime(name,mime);
            Utils::DigitToStr(st.st_size,fsize);
            if(st.st_mode & S_IFDIR){
                file_html += "<strong><a href='"+ info._path_info;
                file_html += name + "'>";
                file_html += name + "</a></strong><br />";
            }
            else{
                file_html += "<a href='"+ info._path_info;
                file_html += name + "'>";
                file_html += name + "</a><br />";
            }
            file_html += "<small>";
            file_html += "Modified: " + mtime + "<br />";
            file_html += mime + "-" + fsize + " kbytes";
            file_html += "<br /><br /></small></li>";
            SendData(file_html);
        }
        string tail = "</ol><hr /></body></html>";
        SendData(tail);
        string s = "";
        SendData(s);     //表示文件结束 
        return true;
    }
    bool SendData(const string& buf)
    {
        if(send(_cli_sock,buf.c_str(),buf.length(),0) < 0){
            return false;
        }
        return true;
    }
    //一块一块发，要发长度,要判断是不是1.1版本
    bool SendCData(const string& buf)
    {
        if(buf.empty()){
            return SendData("0\r\n\r\n");
        }
        std::stringstream ss;
        //将buf的长度弄成十六进制，再加上换行符
        ss << std::hex << buf.length() << "\r\n";
        string tmp = ss.str();
        SendData(tmp);
        ss.clear();
        SendData(buf);
        return true;
    }
    bool ProcessCGI(RequestInfo& info)	 //cgi请求处理
    {
        //使用外部程序完成cgi请求处理 --- 文件上传
        //将http头信息和正文全部交给子进程
        //使用环境变量传递头信息
        //使用管道接收cgi程序的处理结果
        //流程：创建管道，创建子进程，设置子进程环境变量，程序替换
        int in[2];  //用于向子进程传输数据
        int out[2]; //用于从子进程中读取处理结果
        if(pipe(in) < 0){
            info._error_msg = "500";
            cout << "pipe error! " << endl;
            return false;
        }
        if(pipe(out) < 0){
            info._error_msg = "500";
            cout << "pipe error! " << endl;
            return false;
        }
        pid_t pid = fork();
        if(pid < 0){
            info._error_msg = "500";
            cout << "fork error! " << endl;
            return false;
        }
        else if(pid == 0){
            //设置环境变量，传递请求信息
            setenv("METHOD",info._method.c_str(),1);
            setenv("VERSION",info._version.c_str(),1);
            setenv("PATH_INFO",info._path_info.c_str(),1);
            setenv("QUERY_STRING",info._query_string.c_str(),1);
            auto it = info._hdr_list.begin();
            while(it != info._hdr_list.end()){
                setenv(it->first.c_str(),it->second.c_str(),1);
                it++;
            }
            close(in[1]);
            close(out[0]);
            dup2(in[0],0);  //把管道输入重定向到标准输入,从0里边读,就相当从in[0]里边读 
            dup2(out[1],1); //把管道输出重定向到标准输出,往1里边写,就相当往out[1]里边写
            //第一个参数，是程序路径，其余是命令行参数，以NULL结尾
            execl(info._path_phys.c_str(),info._path_phys.c_str(),NULL);
            exit(0);
        }
        close(in[0]);
        close(out[1]);
        //1.通过in管道将正文数据传递给子进程
        auto it = info._hdr_list.find("Content-Length");
        if(it != info._hdr_list.end()){
            char buf[MAX_BUFF] = {0};
            int content_len = Utils::StrToDigit(it->second);
            //LOG("\nFather Content-Length[%d]:\n",content_len);
            int rlen = 0;
            while(rlen < content_len){
                int len = MAX_BUFF > (content_len - rlen) ? (content_len - rlen):(MAX_BUFF);
                int read_len = recv(_cli_sock,buf,len,0);
                //cerr << "Father: "<< endl;
                //cerr << buf << endl;
                //cerr << "完了" << endl;
                if(rlen < 0){
                    info._error_msg = "500";
                    return false;
                }
                rlen += read_len;
                //管道写满了，会阻塞
                if(write(in[1],buf,read_len) < 0){
                    info._error_msg = "500";
                    LOG("write error :%s\n",strerror(errno)); 
                    return false;
                }
            }
        }
        //2.通过out管道读取处理结果，直到返回0
        //3.将处理结果组织成http数据，响应给客户端
        string rsp_header;
        //rsp_header = info._version + " 206 Partial Content\r\n";
        rsp_header = info._version + " 200 OK\r\n";
        rsp_header += "Content-Type: text/html;charset=UTF-8\r\n"; 
        rsp_header += "Etag: " + _etag + "\r\n";
        rsp_header += "Date: " + _date + "\r\n";
        //if(info._version == "HTTP/1.1")
        //    rsp_header += "Transfer-Encoding: chunked\r\n";
        rsp_header += "Connextion: close\r\n\r\n";
        SendData(rsp_header);

        while(1){
            char buf[MAX_BUFF] = {0};
            int len = read(out[0],buf,MAX_BUFF);
            if(len == 0)
                break;
            //SendData(buf);
            send(_cli_sock,buf,len,0);
        }
        close(in[1]);
        close(out[0]);
        waitpid(pid,NULL,0);
        return true;
    }
    //外部处理程序
    bool CGIHandler(RequestInfo& info)	 //处理正常请求
    {
    	InitResponse(info);
    	if(ProcessCGI(info))
            return true;
        ErrHandler(info);
        return false;
    }
    bool FileHandler(RequestInfo& info)
    {
    	InitResponse(info);
    	if(FileIsDir(info)){		//判断请求文件是否是目录文件
    	    if(ProcessList(info))	//处理文件列表响应
    	        return true;
            ErrHandler(info);
            return false;
        }
    	else{
    	   if(ProcessFile(info))	//处理文件下载响应
               return true;
           ErrHandler(info);
           return false;
    	}
    }
    bool ErrHandler(RequestInfo& info)  //处理错误响应
    {
        //首行 协议版本 状态码 状态描述\r\n
        //头部 Content-Length Date
        //空行
        //正文 rsp_header = "<html><body><h1>404;<h1></body></html>"
        //构造响应正文
        string rsp_body;
        rsp_body = "<html><body><h1>" + info._error_msg;
        rsp_body += "<h1></body></html>\r\n";
        int64_t body_len = rsp_body.length();
        string cont_len;
        Utils::DigitToStr(body_len,cont_len);

        //构造响应头部
        string rsp_header;
        rsp_header = info._version + " " + info._error_msg + " ";
        rsp_header += Utils::GetErrDesc(info._error_msg) + "\r\n";
        rsp_header += "Content-Type: text/html;charset=UTF-8\r\n";
        //正文长度
        rsp_header += "Content-Length: " + cont_len + "\r\n";
        //传NULL返回当前时间
        time_t t = time(NULL);
        string gmt;
        Utils::TimeToGMT(t,gmt);
        rsp_header += "Date: " + gmt + "\r\n\r\n";
        SendData(rsp_header);
        SendCData(rsp_body);
        return true;
    } 
};
#endif //__UTILS_HPP__
