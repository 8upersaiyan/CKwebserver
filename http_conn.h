#ifndef NKWEBSERVER_HTTP_CONN_H
#define NKWEBSERVER_HTTP_CONN_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"
#include <sys/uio.h>

class http_conn {
public:
    // 所有的socket上的事件都被注册到一个epoll内核事件中
    static int m_epollfd;
    // 统计用户的数量
    static int m_user_count;
    // 文件名称的最大长度
    static const int FILENAME_LEN = 200;
    // 读缓冲区的大小
    static const int READ_BUFFER_SIZE = 2048;
    // 写缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;

    // HTTP请求方法, 目前只支持get
    enum METHOD {
        GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT
    };


    // 解析客户端请求时，主状态机的状态
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0     // 当前正在分析行
        , CHECK_STATE_HEADER            // 当前正在分析头部字段
        , CHECK_STATE_CONTENT           // 当前正在解析请求体
    };
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    enum LINE_STATUS {
        LINE_OK = 0,    //读取到一个完整的行
        LINE_BAD,       //行出错
        LINE_OPEN       //行数据尚且不完整
    };

    //主状态机 解析请求
    /*服务器处理HTTP请求的可能结果，报文解析的结果*/ 
    enum HTTP_CODE {
        NO_REQUEST,                     //请求不完整，需要继续读取客户数据    
        GET_REQUEST,                    //表示获得了一个完成的客户请求
        BAD_REQUEST,                    //表示客户请求语法错误
        NO_RESOURCE,                    //表示服务器没有资源
        FORBIDDEN_REQUEST,              //表示客户对资源没有足够的访问权限
        FILE_REQUEST,                   //文件请求,获取文件成功
        INTERNAL_ERROR,                 //表示服务器内部错误
        CLOSED_CONNECTION               //表示客户端已经关闭连接了
    };

public:
    http_conn() {}

    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr); // 初始化接受新的连接
    // 关闭连接
    void close_conn();

    // 处理客户端请求 
    void process();

    // 非阻塞读
    bool read();

    // 非阻塞写
    bool write();

private:
    // 该http连接的socket和对方的socket地址
    int m_sockfd;
    sockaddr_in m_address;
    
    char m_read_buf[READ_BUFFER_SIZE];// 读缓冲区
    
    int m_read_idx;// 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    
    int m_checked_idx;// 当前正在分析的字符在读缓冲区的位置
    
    int m_start_line;// 当前正在解析的行的起始位置

    
    CHECK_STATE m_check_state;// 主状态机当前所处的状态
    
    METHOD m_method;// 请求方法

    
    char m_real_file[FILENAME_LEN];// 客户请求的目标文件的完整路径 /home/wuxingyu/resources/...
    
    char *m_url;// 客户请求的目标文件的文件名称
    
    char *m_version;// http协议版本
    
    char *m_host;// hostname 主机名
    
    int m_content_length;// http request msg total length 
    
    bool m_linger;// http request need keep alive flag  HTTP请求是否要保持连接

    // write buffer 写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    // write buff need send bytes num
    int m_write_idx;
    // client request file address in memory
    char *m_file_address;

    // target file status
    struct stat m_file_stat;
    // multi memory block
    struct iovec m_iv[2];
    int m_iv_count;

    // will to send bytes num
    int bytes_to_send;
    // already send bytes num
    int bytes_have_send;


private:
    // 初始化连接其余信息    私有
    void init();

    // 解析http请求
    HTTP_CODE process_read();
    // 分析process_read调研以分析http请求
    HTTP_CODE parse_request_line(char *text);       //解析请求首行
    HTTP_CODE parse_headers(char *text);            //解析请求头
    HTTP_CODE parse_content(char *text);            //解析请求体

    LINE_STATUS parse_line();                       //获取一行


    // 填充http应答
    bool process_write(HTTP_CODE ret);
    
    HTTP_CODE do_request();                         // 请求结果

    inline char *get_line() {                      //内联函数 获取一行数据
        return m_read_buf + m_start_line;
    }

    // this func group use to fill http response
    void unmap();

    bool add_response(const char *format, ...);

    bool add_content(const char *content);

    bool add_content_type();

    bool add_status_line(int status, const char *title);

    bool  add_headers(int content_length);

    bool add_content_length(int content_length);

    bool add_linger();

    bool add_blank_line();

};

#endif //NKWEBSERVER_HTTP_CONN_H
