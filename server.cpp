#include <iostream>
#include <pthread.h>
#include <sched.h>//For CPU_SET Macro
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h> //sysconf
#include <arpa/inet.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>


using namespace std;

#define MAXLEN 10
#define WORKER_PER_GROUP 8

int running = 0;
int ncpus = 0;

class Socket
{
};

class PthreadBase
{
public:
	PthreadBase()
		:tid(0) {
		pthread_attr_init(&attr);
//		printf("start %s\n" , threadinfo);
	}

	~PthreadBase()
	{
		printf("Desconstruct PthreadBase\n");
	}

	//void *(*start_routine) (void *)
	virtual void* run(void* args) { return NULL; }

	 
	bool start(void* obj , void* arg = NULL)
	{
		struct args* args;
		
		args = (struct args*)malloc(sizeof(struct args));
		args->obj = (PthreadBase*)obj;
		args->arg = arg;

		if(pthread_create(&tid , &attr , thread_fn , args)){
			perror("pthread_create failed");
			return false;
		}

		return true;
	}

	void join()
	{
		pthread_join(tid , NULL);
	}

	pthread_t get_tid()
	{
		return tid;
	}
	
	char threadinfo[32];
private:
	struct args
	{
		PthreadBase* obj;
		void* arg;
	};

	static void* thread_fn(void* _arg)
	{
		struct args* val= (struct args*)_arg;
		PthreadBase* obj = static_cast<PthreadBase*>(val->obj);
		void* arg = val->arg;
		free(val);

		obj->run(arg);
//		PthreadBase* obj = val->obj;
		//val->obj->run(val->arg);

		return NULL;
	}

	pthread_t tid;
	pthread_attr_t attr;
};

class Worker:public PthreadBase
{
public:
	//Worker() //未初始化PthreadBase
	Worker():PthreadBase()
	{
		memset(buff , 0 , sizeof(buff));
		strncpy(threadinfo , "Worker" , sizeof("Worker"));
	}

	~Worker()
	{
		printf("Desconstruct Worker\n");
	}

	void* run(void* args)
	{
		int numfd = 0;
		int epfd = *(int*)args;
		struct epoll_event evt;
		printf("start running [%lu] on epfd %d\n" , pthread_self() , epfd);

		while(running)
		{
			numfd = epoll_wait(epfd , &evt , 1 , 1000);
			
			if(numfd >0)
			{
				int len = 0;
				int fd = evt.data.fd;
				struct epoll_event ev;

				if(evt.events & EPOLLIN)
				{
					len = read(fd , buff , MAXLEN);
					if(len < 0){
						perror("Read failed");

						if(errno != EAGAIN && errno != EINTR) {
							epoll_ctl(epfd , EPOLL_CTL_DEL , fd , &ev);
							close(fd);
						}
					} else {
						buff[len] = '\0';
//						len = write(fd, buff, strlen(buf));
						len = write(fd, buff, len);
						if(len < 0) {
//							if (errno != EINTR && errno != EAGAIN) { }
							perror("Write failed");
							epoll_ctl(epfd , EPOLL_CTL_DEL , fd , &ev);
							close(fd);
						}else{
							printf("%lu::receive %s\n" , time(NULL), buff);
							memset(buff , 0 , sizeof(buff));
						}
					}
				}

				if(evt.events & EPOLLOUT)
				{
				}

				if(evt.events & EPOLLRDHUP)
				{
					printf("peer end has closed;remove the fd from epoll set\n");
					epoll_ctl(epfd , EPOLL_CTL_DEL , fd , &ev);
					close(fd);
				}
			}
		}

		printf("stop running and exit [%lu]\n" , pthread_self());
		return NULL;
	}

private:
//	cpu_set_t set;
	char buff[MAXLEN];
};

class WorkGroup
{
public:
	WorkGroup()
	{
		strncpy(threadinfo , "WorkGroup" , sizeof("Worker"));
		epfd = epoll_create1(0);
		if(epfd < 0) {
			perror("epoll create failed");
		}
		create_pool();
	}
	
	~WorkGroup()
	{
		close(epfd);
		join();
	}
	
	void create_pool()
	{
		int i;
//		ncpus = sysconf(_SC_NPROCESSORS_CONF);

		workers = (Worker**)malloc(sizeof(Worker*) * WORKER_PER_GROUP);
		memset(workers , 0 , sizeof(Worker*) * WORKER_PER_GROUP);
		//pthread_attr_t attr;
		//cpu_set_t set;

		for(i = 0; i < WORKER_PER_GROUP; i++)
		{
			//set pthread attribute
			/*		CPU_ZERO(&set);
					CPU_SET(i , &set);
					pthread_attr_init(&attr);
					pthread_attr_setaffinity_np(&attr , sizeof(set) , &set);

					if(pthread_create(tids[i] , &attr , work_thread , NULL)){
					perror("pthread_create failed");
					i--;
					}*/
			workers[i] = new Worker();
			if(workers[i] != NULL)
			{
				workers[i]->start(workers[i] , &epfd);
				printf("Start process %d\n" , i);
			}
		}
	}

	operator int() const
	{
		return epfd;
	}

	void join()
	{
		int i;

		if( workers ) {
			for(i = 0; i < WORKER_PER_GROUP; i++)
			{
				if( workers[i] ) {
					workers[i]->join();
					printf("joined worker thread %lu\n" , workers[i]->get_tid() );
					delete workers[i];
					workers[i] = NULL;
				}
			}
			free(workers);
			workers = NULL;
		}
	}

private:
	int epfd;
	Worker** workers;
	char threadinfo[32];
};

WorkGroup** wgs = NULL;

class ConnectionManager:public PthreadBase
{
public:
	ConnectionManager(int _fd):PthreadBase() , fd(_fd)
	{
		strncpy(threadinfo , "ConnectionManager" , sizeof("ConnectionManager"));
	}

	~ConnectionManager()
	{
		printf("Desconstruct ConnectionManager\n");
	}

	static ConnectionManager* create(int port , const char* ip)
	{
		int reuse = 0;
		int listenfd = socket(AF_INET , SOCK_STREAM|SOCK_NONBLOCK , 0);
		if(listenfd < 0)
		{
			perror("listenfd create failed");
			goto failed;
		}

		reuse = 1;
		if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(int))==-1) {
			goto failed;
		}

		struct sockaddr_in addr;
		memset(&addr , 0 , sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		inet_aton(ip, &(addr.sin_addr));
//		addr.sin_addr.s_addr = INADDR_ANY;
		
		if(bind(listenfd , (struct sockaddr *)&addr , sizeof(addr)) < 0)
		{
			perror("bind fd failed");
			goto failed;
		}

		if(listen(listenfd , 128))
		{
			perror("bind fd failed");
			goto failed;
		}

		return new ConnectionManager(listenfd);
failed:
		close(listenfd);
		return NULL; 
	}

	//virtual void* run(void*) = 0;
	void* run(void* arg)
	{
		int epfd = 0;
		int numfd = 0;
		int rrIndex = 0;
		struct epoll_event evt;
		struct epoll_event event;
	
		epfd = epoll_create1(0);
		if(epfd < 0)
		{
			perror("epoll create failed");
			goto failed;
		}
		
		evt.events |= EPOLLIN;
		evt.data.fd = fd;
		if(epoll_ctl(epfd , EPOLL_CTL_ADD , fd , &evt) < 0)
		{
			perror("epoll_ctl failed");
			goto failed;
		}

		while(running)
		{
			memset(&event , 0 , sizeof(event));
			numfd = epoll_wait(epfd , &event , 1 , 1000);

			if(numfd){
				if(event.events & EPOLLIN)
				{
					int fd = accept4(event.data.fd , NULL ,NULL , SOCK_NONBLOCK);
					if(fd < 0){
						perror("accept failed");
						continue;
					}

					/*send the fd to the worker thread*/
					/*add the accept fd to the worker's epfd event set*/
					evt.data.fd = fd;
		//			evt.events = EPOLLIN | EPOLLONESHOT;
					evt.events = EPOLLIN | EPOLLRDHUP;

					/* register to worker-pool's epoll,
					 *                  * not the listen epoll */
//					g_conn_table[sock].index= rrIndex;
					epoll_ctl(*wgs[rrIndex], EPOLL_CTL_ADD, fd, &evt);
					rrIndex = (rrIndex + 1) % ncpus;
				}
			}
		}

failed:
		close(epfd);
		return NULL;
	}

private:
	int fd;
};


typedef void (*sighandler_t)(int);

void sighandler(int sig)
{
	printf("stop the threads\n");
	running = false;
}

void create_workgroup()
{
	int i;
	ncpus = sysconf(_SC_NPROCESSORS_CONF);

	wgs = (WorkGroup**)malloc(sizeof(WorkGroup*) * ncpus);
	memset(wgs , 0 , sizeof(WorkGroup*) * ncpus);

	for(i = 0; i < ncpus; i++)
	{
		printf("Start WorkGroup %d:\n" , i);
		wgs[i] = new WorkGroup();
	}
}

int main(int argc , char** argv)
{
	if(argc < 3)
	{
		printf("argc not enough\n");
		return 0;
	}

	int i = 0;
	int port = atoi(argv[1]);

	signal(SIGINT , sighandler);

	ConnectionManager* cm = ConnectionManager::create(port , argv[2]);
	if (!cm) {
		printf("ConnectionManager create failed\n");
		return -1;
	}

	running = true;
	cm->start(cm);
	create_workgroup();
	
	pause();
	
	for(i = 0; i < ncpus; i++)
	{
		printf("join work group %d:\n" , i);
		wgs[i]->join();
		delete wgs[i];
	}
	free(wgs);
	wgs = NULL;

	cm->join();
	printf("joined listener thread %lu\n" , cm->get_tid() );
	delete cm;

	return 0;
}
