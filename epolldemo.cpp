#include <iostream>
#include <string>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

class SocketBlock
{
public:
    static void setnonblocking(int sock)
    //将socket设为非阻塞模式
    {
        int opts;
        opts=fcntl(sock,F_GETFL);
        if(opts<0)
        {
            perror("fcntl(sock,GETFL)\n");
            exit(1);
        }
        opts = opts|O_NONBLOCK;
        if(fcntl(sock,F_SETFL,opts)<0)
        {
            perror("fcntl(sock,SETFL,opts)\n");
            exit(1);
        }
    }
};

int main(int argc, char *argv[])
{
    //端口默认为6379，可以启动时设置
    int port;
    if(argc == 2)
        port = atoi(argv[1]);
    else
        port = 6380;    

    //监听句柄，epoll句柄，客户端句柄，被触发的句柄数，数据流长度
    int listenfd, epfd, clientfd, fd_nums, stream_len;

    //用于接受数据的buff
    const int bufflen = 1024;
    char buff[bufflen];

    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    //socket设为非阻塞，便于epoll ET模式使用
    SocketBlock::setnonblocking(listenfd);

    //使端口可在time_wait时期重用
    int option = 1;
    socklen_t optlen = sizeof(option);
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &option, optlen);

    //sockaddr_in初始化，可接受ip字段为所有（INADDR_ANY）
    sockaddr_in clientaddr, severaddr;
    socklen_t addrin_len = sizeof(sockaddr_in);

    memset(&severaddr, 0, addrin_len);
    severaddr.sin_family = AF_INET;
    severaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    severaddr.sin_port = htons(port);

    //开始监听
    bind(listenfd, reinterpret_cast<sockaddr*>(&severaddr), addrin_len);
    listen(listenfd, 20);

    //设置epoll
    const int max_event_num = 20;
    epoll_event ev, events[max_event_num];
    epfd = epoll_create(256);
    ev.data.fd = listenfd;
    //设置为ET模式
    ev.events = EPOLLIN | EPOLLET;
    //注册监听fd
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);

    for(;;)
    {
        //获取触发的event信息
        fd_nums = epoll_wait(epfd, events, max_event_num, 0);
        if (fd_nums == -1) {  
            perror("epoll_pwait");  
            exit(EXIT_FAILURE);  
        }  

        //进行处理
        for(int i = 0; i < fd_nums; i++)
        {
            if(events[i].data.fd == listenfd)
            {
                //接收新的连接，由于ET特性，需要一直读取到出错为止
                while((clientfd = accept(listenfd, reinterpret_cast<sockaddr*>(&clientaddr), &addrin_len)) > 0)
                {
                    std::cout << "new connect: " << inet_ntoa(clientaddr.sin_addr) << std::endl;
                    SocketBlock::setnonblocking(clientfd);
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = clientfd;

                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev) == -1) {  
                        perror("epoll_ctl: add");  
                        exit(EXIT_FAILURE);  
                    }  
                }
                if(clientfd == -1)
                {
                    if(errno != EAGAIN && errno != ECONNABORTED && errno != EPROTO && errno != EINTR)
                    {
                        perror("accept");
                    }
                }
                continue;
            }
            //读触发
            if(events[i].events & EPOLLIN)
            {
                //读取HTTP请求
                int buff_len = 0;
                stream_len = 0;
                while((buff_len = read(events[i].data.fd, buff, bufflen - 1)) > 0)
                {
                    stream_len += buff_len;
                }
                if(buff_len == -1 && errno != EAGAIN)
                {
                    perror("read error");
                }

                std::cout << "in" << std::endl;

                ev.data.fd = events[i].data.fd;
                //将其设置为OUT，以便在下次发送数据
                ev.events = EPOLLET | EPOLLOUT;
                if(epoll_ctl(epfd, EPOLL_CTL_MOD, events[i].data.fd, &ev) == -1)
                {
                    perror("epoll_ctl: mod");
                }
            }
            //写触发
            else if(events[i].events & EPOLLOUT)
            {
                //设置要发送的数据
                std::string head = "HTTP/1.0 200 OK\r\nContent-Length: %d\r\n\r\n";
                std::string context = "Hello, this is my qian_ru_shi work\nIt uses epoll ET mode";
                sprintf(buff, (head + context).c_str(), context.size());
                stream_len = strlen(buff);

                int pos_stream = 0;
                int buff_len = 0;
                while(stream_len > 0)
                {
                    buff_len = write(events[i].data.fd, buff + pos_stream, stream_len);
                    if(buff_len == -1 && errno != EAGAIN)
                    {
                        perror("write error");
                        break;
                    }
                    stream_len -= buff_len;
                    pos_stream += buff_len;
                }
                std::cout << "out" << std::endl;
                close(events[i].data.fd); 
                //长字符串, 用于测试
                // std::string str;
                // str += "HTTP/1.1 200 OK\r\nContent-Length: 10000000\r\n\r\n";
                // str += std::string(5000000, 'a');
                // str += std::string(2000000, 'b');
                // str += std::string(3000000, 'c');
                // stream_len = str.size();
                // int pos_stream = 0;
                // while(stream_len > 0)
                // {
                //     buff_len = write(events[i].data.fd, &str.c_str()[pos_stream], stream_len);
                //     if(buff_len == -1 && errno != EAGAIN)
                //     {
                //         perror("write error");
                //         break;
                //     }
                //     stream_len -= buff_len;
                //     pos_stream += buff_len;
                // }
                // close(events[i].data.fd); 
            }
        }
    }
    return 0;
}