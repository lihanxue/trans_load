//#include "Agent_client.hh"
#include "buffer.hh"
#include <list>
#include <time.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define MAX_EVENT 1000
using namespace std;
typedef struct data
{
	int sockfd;
	Buffer* send_buffer;
	Buffer* recv_buffer;
	int login_flag;
	int id;
	int read_end;//0:no_end;1:end
	int write_end;//0:no_end;1:end
} data_t;
void setnonblocking(int fd){//设置套接字为非阻塞模式
    int flag = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}
int 
main(int argc, char** argv)
{
	int sockfd, talk_links, msg_size, trans_times,all_login;
	int times = 0;
	struct sockaddr_in servaddr;
	int n;
	int con_n = 0;
	all_login = 0;
	if (argc != 6) {
		printf("Usage: %s <ip> <port> <talk_links> <msg_size> <times>\n", argv[0]);
		exit(0);
	}

	talk_links = atoi(argv[3]);
	msg_size = atoi(argv[4]);
	trans_times = atoi(argv[5]);
	con_n = 2*talk_links;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	if(n = inet_pton(AF_INET, argv[1], &servaddr.sin_addr) < 0){
		cout << "inet_pton err" << endl;
		exit(0);
	}
	servaddr.sin_port = htons(atoi(argv[2]));

	int epfd;
	if((epfd = epoll_create(talk_links*2)) < 0){
		cout << "epoll_create() err" << endl;
		exit(0);
	}
	struct epoll_event ev;
	for (int i = 0; i < 2*talk_links; ++i) {
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if(sockfd < 0){
			cout << "socket() err" << endl;
			exit(0);
		}
		
		if(connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0){
			cout << sockfd << "socket connect() err" << endl;
			perror("errno:");
			con_n = con_n - 1;
			continue;
		}
		setnonblocking(sockfd);
		data_t* data_ptr = new data_t;
		data_ptr->sockfd = sockfd;
		data_ptr->login_flag = 0;
		data_ptr->id = i+1;
		data_ptr->read_end = 1;
		data_ptr->write_end = 1;//对一段中继报文的读写都结束
		data_ptr->send_buffer = new Buffer(msg_size+12);
		data_ptr->send_buffer->setClear();
		data_ptr->recv_buffer = new Buffer(msg_size+12);
		data_ptr->recv_buffer->setClear();
/*每个用户只发送一个登录报文,和一个带头报文的一共msg_size大小的报文*/
		ev.data.ptr = data_ptr;		
		ev.events = EPOLLOUT;
		//data_ptr->read_end = 1;
		//data_ptr->write_end = 1;
		//前一个用户向后一个用户先发报文再等后一个用户发回，为一次中继
		epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);
	}
	std::cout << "Connection successful..." << con_n << " connections" << endl;
//连接talk_links个会话成功
	struct epoll_event events[MAX_EVENT];
	//int trans_times = times;//A->B->A为一次中继,trans_times为所有连接用户总中继次数
	std::list<clock_t> start_time;
	std::list<clock_t> end_time;//计算中继平均时延
	clock_t main_start = clock();
	clock_t main_end;
	//while (times > 0) {//改为不限次数的一直中继
	while(1){
		if(times >= trans_times){
			main_end = clock();
			break;
		}
		int number;
		number = epoll_wait(epfd,events,MAX_EVENT,500);
		if(number < 0){
			cout << "epoll_wait() err" << endl;
			perror("errno:");
			exit(0); 
		}
		//cout << "into for(;;) number = " << number << endl;
		for (int i = 0; i < number; ++i) {
			data_t* data_ptr = (data_t*)events[i].data.ptr;
			if (events[i].events & EPOLLIN) {
				n = data_ptr->recv_buffer->read_nonblock(data_ptr->sockfd);
				//cout << data_ptr->sockfd << " read " << n << " bytes" << endl;
				if (!(data_ptr->recv_buffer->isReadable())) {
					events[i].events = EPOLLOUT;
					epoll_ctl(epfd, EPOLL_CTL_MOD, data_ptr->sockfd, &events[i]);//改变状态为in
					//cout << "改变socket " << data_ptr->sockfd << " epoll为" << "EPOLLOUT" << endl;
					data_ptr->recv_buffer->setClear();
					if((data_ptr->id%2) == 1){
						end_time.push_back(clock());
						++times;//中继次数加一
						if(times >= trans_times)
						break;
					}//完成一次中继A->B->A
				}
			}
			if (events[i].events & EPOLLOUT) {
				if(data_ptr->login_flag == 0){
					if(data_ptr->write_end == 1){
						if((data_ptr->id%2) == 1){
							data_ptr->send_buffer->setHeader(data_ptr->id,
							0,data_ptr->id+1);
						}
						else{
							data_ptr->send_buffer->setHeader(data_ptr->id,
							0,data_ptr->id-1);
						}
					}
					int a = data_ptr->send_buffer->write_nonblock(data_ptr->sockfd);
					//cout << "发送登录报文" << endl;
					//cout << data_ptr->sockfd << " write " << a << " bytes" << endl;
					a = data_ptr->send_buffer->isHeader();
					all_login = all_login + 1;
					if(a == 1){
						//cout << "header writeout 12 finished" << endl;
						data_ptr->login_flag = 1;
						if((data_ptr->id%2) == 0){
							events[i].events = EPOLLIN;
							epoll_ctl(epfd, EPOLL_CTL_MOD, data_ptr->sockfd, &events[i]);//改变状态为in
							//cout << "改变socket " << data_ptr->sockfd << " epoll为" << "EPOLLIN" << endl;
						}
						data_ptr->write_end  = 1;
					}
					else
						data_ptr->write_end = 0;
				}//第一次登录
				else if(all_login == talk_links*2){
					if(data_ptr->write_end == 1){
						if((data_ptr->id%2) == 1){
							data_ptr->send_buffer->setHeader(data_ptr->id,
							msg_size,data_ptr->id+1);
						}
						else{
							data_ptr->send_buffer->setHeader(data_ptr->id,
							msg_size,data_ptr->id-1);
						}
						data_ptr->send_buffer->setFull();
					}
					if((data_ptr->id%2) == 1)
						start_time.push_back(clock());
					n = data_ptr->send_buffer->write_nonblock(data_ptr->sockfd);
					//cout << "发送数据报文" << endl;
					//cout << data_ptr->sockfd << " write " << n << " bytes" << endl;
					if(!(data_ptr->send_buffer->isWritable())){
						data_ptr->write_end = 1;
						events[i].events = EPOLLIN;
						epoll_ctl(epfd, EPOLL_CTL_MOD, data_ptr->sockfd, &events[i]);//改变状态为in
						//cout << "改变socket " << data_ptr->sockfd << " epoll为" << "EPOLLIN" << endl;
					}
					else{
						data_ptr->write_end = 0;
					}
				}//非第一次登录
			}
		}
	}
	
	std::cout << "total time: " << (main_end - main_start) / double(CLOCKS_PER_SEC) * 1000 << "ms" << std::endl;
	cout << "A->B->A trans number:" << times << " " << trans_times << endl; 

	if (1) {
		cout << "start_time size:" <<start_time.size() << " " 
		"end_time size:"<< end_time.size() << endl;
		std::list<clock_t>::iterator start_iter = start_time.begin();
		std::list<clock_t>::iterator end_iter = end_time.begin();
		clock_t average = 0;
		int sum = 0;
		while ((start_iter != start_time.end()) && (end_iter != end_time.end())) {
			average = ((average * sum) + *end_iter - *start_iter) / (sum + 1);
			++sum;
			++start_iter;
			++end_iter;
		}
		cout << "A->B->A trans average delay: " << average / double(CLOCKS_PER_SEC) * 1000 << "ms" << endl;
	}
	return 0;
}






	       
