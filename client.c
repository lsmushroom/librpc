#include <stdio.h>

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <strings.h>
#include <string.h>

#include <sys/epoll.h>
#include <linux/tcp.h>
#include <errno.h>

#define SERV_PORT 1234
#define MAX_EVENT 1

int main(int argc, char *argv[])
{
    int ret = 0;
    int nfds = 0;
    int sockfd = 0;
    int epfd = 0;
    struct sockaddr_in serveraddr;
	struct epoll_event ev, events;
	char sndbuf[] = "Test aaaaa\n";
	char rcvbuf[100];
	const int _maxSendSize = 4096;
	int size = sizeof(_maxSendSize);

	memset(rcvbuf , 0 , 100);

    sockfd = socket(AF_INET , SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC , 0);
//    sockfd = socket(AF_INET , SOCK_STREAM , 0);
    if(sockfd < 0)
    {
        perror("socket failed");
        return 0;
    }

    bzero(&serveraddr , sizeof(serveraddr));
    char *local_addr = "10.1.93.42";
    inet_aton(local_addr, &(serveraddr.sin_addr));
    serveraddr.sin_port = htons(SERV_PORT);
    serveraddr.sin_family = AF_INET;

	if(getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char*) &_maxSendSize, &size)) {
		perror("getsockopt SO_SNDBUF failed:");
	} else {
		printf("_maxSendSize = %d\n" , _maxSendSize);
	}
	
	if(setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char*) &_maxSendSize, size)) {
		perror("setsockopt SO_SNDBUF failed:");
	} else {
		printf("_maxSendSize = %d\n" , _maxSendSize);
	}
	
	if(getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char*) &_maxSendSize, &size)) {
		perror("getsockopt");
	} else {
		printf("_maxRCVSize = %d\n" , _maxSendSize);
	}
	
	if(setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char*) &_maxSendSize, size)) {
		perror("setsockopt");
	} else {
		printf("_maxRCVSize = %d\n" , _maxSendSize);
	}
	
	if(getsockopt(sockfd, IPPROTO_TCP, TCP_MAXSEG, (char*) &_maxSendSize, &size)) {
		perror("getsockopt");
	} else {
		printf("MSS = %d\n" , _maxSendSize);
	}

    ret = connect(sockfd , (const struct sockaddr *)&serveraddr , sizeof(struct sockaddr_in));
    if(ret < 0 && errno != EINPROGRESS){
	printf("ret = %d , errno = %d\n" , ret , errno);
        perror("connect failed:");
        goto out;
    }

    epfd = epoll_create(MAX_EVENT);
    if(epfd < 0)
    {
        perror("epoll failed:");
        goto close;
    }
	
    ev.data.fd = sockfd;
//	ev.events = EPOLLOUT | EPOLLIN | EPOLLET;
	ev.events = EPOLLOUT | EPOLLIN;

	epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

    while(1)
    {
        nfds = epoll_wait(epfd , &events , MAX_EVENT , -1);
        if(nfds < 0)
        {
            perror("epoll wait failed:");
            break;
        }

		sockfd = events.data.fd;
//        if(events.data.fd == sockfd)
        if(events.events & EPOLLOUT)
        {
            int val = 0;
            int len = sizeof(int);
            if( (ret = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &val, &len)) < 0 )
            {
                perror("getsockopt failed:");
                break;
            }
            if(val != 0)
            {
                perror("connect failed:");
                break;
            }

            if(send(sockfd , sndbuf , strlen(sndbuf) , 0) < 0){
                perror("send failed");
				if(errno == EAGAIN || errno == EINTR) {
					continue;
				}else{
					break;
				}
            }
        }

		if(events.events & EPOLLIN)
		{
			ret = read(sockfd , rcvbuf , 100);
			if(ret < 0)
			{
				perror("read error:");
				continue;
			}else{
				rcvbuf[ret] = '\0';
				printf("%s\n" , rcvbuf);
				memset(rcvbuf , 0 , 100);
			}
		}
    }

close:
    close(sockfd);

out:
    return 0;
}
