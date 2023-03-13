#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65536   // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量

extern void addfd(int epollfd, int fd, bool one_shot);

extern void removefd(int epollfd, int fd);

//设置信号函数 添加信号捕捉
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask); //临时阻塞信号集
    assert(sigaction(sig, &sa, NULL) != -1);
}
 
int main(int argc, char *argv[]) {

    if (argc < 2) {
        //basename从argv[0]中截取文件名 头文件#include <libgen.h>
        //printf("Usage: %s port number\n", basename(argv[0]));
        printf("按照如下的格式运行： %s port_number\n",basename(argv[0]));
        return 1;
    }

    // 获取端口号
    int port = atoi(argv[1]);
    // Prevents process exit due to tcp sigpipe signals
    //对SIGPIE信号进行处理 防止僵尸进程产生交给系统init回收
    //当服务器close一个连接时，若client端接着发数据。
    //根据TCP协议的规定，会收到一个RST响应，client再往这个服务器发送数据时，系统会发出一个SIGPIPE信号给进程，告诉进程这个连接已经断开了，不要再写了。
    //根据信号的默认处理规则SIGPIPE信号的默认执行动作是terminate(终止、退出),所以client会退出。若不想客户端退出可以把SIGPIPE设为SIG_IGN
    addsig(SIGPIPE, SIG_IGN);

    //创建线程池 HTTP连接对象的
    threadpool<http_conn> *pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    } catch (...) {
        return 1;
    }

    //创建一个数组用于保存所有的客户端信息
    http_conn *users = new http_conn[MAX_FD];

    //创建监听套接字
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // 设置端口复用 在绑定之前设置
    //当服务器需要重启时，经常会碰到端口尚未完全关闭的情况，
    //这时如果不设置端口复用，则无法完成绑定，因为端口还处于被别的套接口绑定的状态之中。
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    //绑定
    ret = bind(listenfd, (struct sockaddr *) &address, sizeof(address));
    assert(ret >= 0);
    //监听
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    //回传事件数组 
    epoll_event events[MAX_EVENT_NUMBER];

    //创建epoll对象
    int epollfd = epoll_create(5);
    assert(epollfd != -1);

    //将监听的文件描述符添加到epoll对象中
    addfd(epollfd, listenfd, false);

    //所有的socket上的事件都被注册到一个epoll内核事件中
    http_conn::m_epollfd = epollfd;

    while (true) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR)) {
            printf("epoll failure\n");
            break;
        }
        // 循环遍历事件数组
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                //有客户端连接进来
                struct sockaddr_in client_address;
                socklen_t client_address_len = sizeof(client_address);
                int client_fd =
                        accept(listenfd, (struct sockaddr *) &client_address, &client_address_len);
                //连接失败
                if (client_fd < 0) {
                    printf("errno is : %d\n", errno);
                    continue;
                }
                //目前最大支持的连接数满了
                if (http_conn::m_user_count >= MAX_FD) {
                    //show_error(client_fd, "Internal server busy");
                    close(client_fd);
                    continue;
                }
                //新的客户的数据初始化，放到数组中 文件描述符当作索引
                users[client_fd].init(client_fd, client_address);
            }
 
            //异常事件 
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                users[sockfd].close_conn();  //对方异常断开或者错误等事件
            }

            //判断是否有读的事件发生
            else if (events[i].events & EPOLLIN) {
                if (users[sockfd].read()) {
                    //一次性把所有数据都读完 若监测到读事件，将该事件放入请求队列
                    pool->append(users + sockfd); 
                } else {
                    users[sockfd].close_conn();
                }
            }

            //判断写事件
            else if (events[i].events & EPOLLOUT) {
                if (!users[sockfd].write()) {
                    users[sockfd].close_conn();
                }
            }
        }

    }
    close(epollfd);
    close(listenfd);
    delete[]users;
    delete pool;
    return 0;
}
