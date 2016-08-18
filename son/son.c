//文件名: son/son.c
//
//描述: 这个文件实现SON进程
//SON进程首先连接到所有邻居, 然后启动listen_to_neighbor线程, 每个该线程持续接收来自一个邻居的进入报文, 并将该报文转发给SIP进程.
//然后SON进程等待来自SIP进程的连接. 在与SIP进程建立连接之后, SON进程持续接收来自SIP进程的sendpkt_arg_t结构, 并将接收到的报文发送到重叠网络中.
//
//创建日期: 2015年

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/utsname.h>
#include <assert.h>
#include <errno.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "son.h"
#include "../topology/topology.h"
#include "neighbortable.h"

#define DEBUG

//你应该在这个时间段内启动所有重叠网络节点上的SON进程
#define SON_START_DELAY 10

/**************************************************************/
//声明全局变量
/**************************************************************/

//将邻居表声明为一个全局变量
nbr_entry_t* nt;
//将与SIP进程之间的TCP连接声明为一个全局变量
int socket_sip;
int socket_listen;
int sock_listo_sip;

/**************************************************************/
//实现重叠网络函数
/**************************************************************/

// 这个线程打开TCP端口CONNECTION_PORT, 等待节点ID比自己大的所有邻居的进入连接,
// 在所有进入连接都建立后, 这个线程终止.
void* waitNbrs(void* arg) {
	//你需要编写这里的代码.
	struct sockaddr_in ipaddr;
	struct sockaddr_in client;
	nbr_entry_t* tmp=nt;
	int i,c,n,soc,id_in,new_flag;
	int num=topology_getNbrNum();
	const int myID=topology_getMyNodeID();
	int len=sizeof(ipaddr);
	unsigned char *p;

	printf("SON start!\n");
	socket_listen=socket(AF_INET, SOCK_STREAM, 0);

	i=sizeof(sip_pkt_t)*100;

	setsockopt(socket_listen, SOL_SOCKET, SO_RCVBUF, (const char *)&i, sizeof(i));
	i=1;
	setsockopt(socket_listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&i, sizeof(i));
	memset(&ipaddr, 0, sizeof(ipaddr));
	ipaddr.sin_family=AF_INET;
	ipaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	ipaddr.sin_port=htons(CONNECTION_PORT);

	bind(socket_listen, (struct sockaddr*) &ipaddr, sizeof(ipaddr));
	printf("TCP listening.....\n");
	listen(socket_listen, num);

	n=0;
	while(num--){
        if(tmp->nodeID>myID)++n;
        tmp++;
	}
    num=topology_getNbrNum();
    i=0;
	while(i<n){
        memset(&client, 0, sizeof(client));
		soc=accept(socket_listen, (struct sockaddr*)&client,(socklen_t *)&len);

        p=(unsigned char *)&client.sin_addr;
        printf("[Connected][In] IP address:%u.%u.%u.%u  Socket:%d",p[0],p[1],p[2],p[3],soc);

		id_in=topology_getNodeIDfromip(&client.sin_addr);
		new_flag=0;
        for(c=0;c<num;++c){
            if((nt[c].nodeID==id_in)&&(nt[c].conn==-1)){
                nt[c].conn=soc;
                printf("    [Accepted]\n");
                ++i;
                new_flag=1;
                break;
            }
		}
		if(!new_flag){
            close(soc);
            printf("    [Refused]\n");
		}
	}

	return (void *)0;
}

// 这个函数连接到节点ID比自己小的所有邻居.
// 在所有外出连接都建立后, 返回1, 否则返回-1.
int connectNbrs() {
	//你需要编写这里的代码.
	int socket_fd;
   	struct sockaddr_in server_addr;
   	int num=topology_getNbrNum();
	const int myID=topology_getMyNodeID();
	nbr_entry_t* tmp=nt;

    while(num--){
        if(tmp->nodeID<myID){
            socket_fd=socket(AF_INET,SOCK_STREAM,0);
            if(socket_fd==-1){
                printf("Socket Error:%s\a\n",strerror(errno));
                return -1;
            }

            bzero(&server_addr,sizeof(server_addr));
            server_addr.sin_family=AF_INET;
            server_addr.sin_port=htons(CONNECTION_PORT);
            server_addr.sin_addr.s_addr=tmp->nodeIP;

            if(connect(socket_fd,(struct sockaddr *)&server_addr,sizeof(struct sockaddr_in))==-1){
                printf("Connect Error:%s\a\n",strerror(errno));
                return -1;
            }
            tmp->conn=socket_fd;
            printf("[Connected][Out]NodeID:%d\n",tmp->nodeID);
        }
        ++tmp;
	}

	return 1;
}

//每个listen_to_neighbor线程持续接收来自一个邻居的报文. 它将接收到的报文转发给SIP进程.
//所有的listen_to_neighbor线程都是在到邻居的TCP连接全部建立之后启动的.
void* listen_to_neighbor(void* arg) {
	//你需要编写这里的代码.
	nbr_entry_t* tmp=(nt+*((int *)arg));
	int sock_to_sip=tmp->conn;
	sip_pkt_t buf;

	if(sock_to_sip<0)return (void *)-1;

	while(1){
        if(1==recvpkt(&buf, tmp->conn))forwardpktToSIP(&buf,socket_sip);
	}

    return (void *)0;
}

//这个函数打开TCP端口SON_PORT, 等待来自本地SIP进程的进入连接.
//在本地SIP进程连接之后, 这个函数持续接收来自SIP进程的sendpkt_arg_t结构, 并将报文发送到重叠网络中的下一跳.
//如果下一跳的节点ID为BROADCAST_NODEID, 报文应发送到所有邻居节点.
void waitSIP() {
	//你需要编写这里的代码.
	struct sockaddr_in ipaddr;
	int i;
	int nextNode;
	int myID=topology_getMyNodeID();
	int neibor_num=topology_getNbrNum();
	sip_pkt_t buf;
	nbr_entry_t* tmp;
	pkt_routeupdate_t *rootupdate=(pkt_routeupdate_t *)&buf.data;

    printf("Start 'waitSIP'\n");
	sock_listo_sip=socket(AF_INET, SOCK_STREAM, 0);

	i=sizeof(sip_pkt_t)*100;

	setsockopt(sock_listo_sip, SOL_SOCKET, SO_RCVBUF, (const char *)&i, sizeof(i));
	i=1;
	setsockopt(sock_listo_sip, SOL_SOCKET, SO_REUSEADDR, (const char *)&i, sizeof(i));
	memset(&ipaddr, 0, sizeof(ipaddr));
	ipaddr.sin_family=AF_INET;
	ipaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	ipaddr.sin_port=htons(SON_PORT);

	bind(sock_listo_sip, (struct sockaddr*) &ipaddr, sizeof(ipaddr));
    printf("TCP listening to sip.....\n");
	listen(sock_listo_sip, 10);
	socket_sip=accept(sock_listo_sip, NULL, NULL);
	printf("sip connected,socket_sip:%d\n",socket_sip);

	while(1){
        if(1==getpktToSend(&buf, &nextNode,socket_sip)){
            if(nextNode==BROADCAST_NODEID){
                for(tmp=nt,i=0;i<neibor_num;++i){
                    if(tmp->conn>=0){
                        if(1==sendpkt(&buf,tmp->conn)){
                            --tmp->quality;
                        }
                        else ++tmp->quality;
                        if(tmp->quality>20){
                            buf.header.dest_nodeID=myID;
                            buf.header.src_nodeID=myID;
                            buf.header.type=NBR_UPDATE;
                            buf.header.length=sizeof(pkt_routeupdate_t);
                            rootupdate->entryNum=1;
                            rootupdate->entry[0].nodeID=tmp->nodeID;
                            rootupdate->entry[0].cost=DOUBLE;
                            if(1==forwardpktToSIP(&buf,socket_sip))tmp->quality=0;
                        }
                        else if(tmp->quality<-20){
                            buf.header.dest_nodeID=myID;
                            buf.header.src_nodeID=myID;
                            buf.header.type=NBR_UPDATE;
                            buf.header.length=sizeof(pkt_routeupdate_t);
                            rootupdate->entryNum=1;
                            rootupdate->entry[0].nodeID=tmp->nodeID;
                            rootupdate->entry[0].cost=JIANER;
                            if(1==forwardpktToSIP(&buf,socket_sip))tmp->quality=0;
                        }
                    }
                    ++tmp;
                }
            }
            else{
                for(tmp=nt,i=0;i<neibor_num;++i){
                    if(tmp->nodeID==nextNode){
                        if(1==sendpkt(&buf,tmp->conn)){
                            --tmp->quality;
                        }
                        else ++tmp->quality;
                        if(tmp->quality>20){
                            buf.header.dest_nodeID=myID;
                            buf.header.src_nodeID=myID;
                            buf.header.type=NBR_UPDATE;
                            buf.header.length=sizeof(pkt_routeupdate_t);
                            rootupdate->entryNum=1;
                            rootupdate->entry[0].nodeID=tmp->nodeID;
                            rootupdate->entry[0].cost=DOUBLE;
                            if(1==forwardpktToSIP(&buf,socket_sip))tmp->quality=0;
                            printf("From %d to %d,cost doubled.\n",myID,tmp->nodeID);
                        }
                        else if(tmp->quality<-20){
                            buf.header.dest_nodeID=myID;
                            buf.header.src_nodeID=myID;
                            buf.header.type=NBR_UPDATE;
                            buf.header.length=sizeof(pkt_routeupdate_t);
                            rootupdate->entryNum=1;
                            rootupdate->entry[0].nodeID=tmp->nodeID;
                            rootupdate->entry[0].cost=JIANER;
                            if(1==forwardpktToSIP(&buf,socket_sip))tmp->quality=0;
                            printf("From %d to %d,cost -2.\n",myID,tmp->nodeID);
                        }
                        break;
                    }
                    ++tmp;
                }
            }
        }
	}

	return;
}

//这个函数停止重叠网络, 当接收到信号SIGINT时, 该函数被调用.
//它关闭所有的连接, 释放所有动态分配的内存.
void son_stop() {
	//你需要编写这里的代码.
	nbr_entry_t* tmp=nt;
	int num=topology_getNbrNum();

	while(num--){
        if(tmp->nodeID>=0)close(tmp->nodeID);
        ++tmp;
	}
	free(nt);
	close(socket_listen);
	close(socket_sip);
	close(sock_listo_sip);
}

int main() {
	topology_init();
	//启动重叠网络初始化工作
	printf("Overlay network: Node %d initializing...\n",topology_getMyNodeID());

	//创建一个邻居表
	nt = nt_create();
	if(nt==NULL){
        printf("Fail to create neighbor list!\nProgram close.....\n");
        return -1;
	}
	//将sip_conn初始化为-1, 即还未与SIP进程连接
	socket_sip = -1;

	//SIGPIPE ignore
    signal(SIGPIPE, SIG_IGN);
    /*struct sigaction act;

    act.sa_handler = SIG_IGN;

    if (sigaction(SIGPIPE, &act, NULL) == 0) {

        LOG("SIGPIPE ignore");

    }*/

	//注册一个信号句柄, 用于终止进程
	signal(SIGINT, son_stop);

	//打印所有邻居
	int nbrNum = topology_getNbrNum();
	int i;
	for(i=0;i<nbrNum;i++) {
		printf("Overlay network: neighbor %d:%d\n",i+1,nt[i].nodeID);
	}

	//启动waitNbrs线程, 等待节点ID比自己大的所有邻居的进入连接
	pthread_t waitNbrs_thread;
	pthread_create(&waitNbrs_thread,NULL,waitNbrs,(void*)0);

	//等待其他节点启动
	sleep(SON_START_DELAY);

	//连接到节点ID比自己小的所有邻居
	connectNbrs();

	//等待waitNbrs线程返回
	pthread_join(waitNbrs_thread,NULL);

	//此时, 所有与邻居之间的连接都建立好了

	//创建线程监听所有邻居
	for(i=0;i<nbrNum;i++) {
		int* idx = (int*)malloc(sizeof(int));
		*idx = i;
		pthread_t nbr_listen_thread;
		pthread_create(&nbr_listen_thread,NULL,listen_to_neighbor,(void*)idx);
	}
	printf("Overlay network: node initialized...\n");
	printf("Overlay network: waiting for connection from SIP process...\n");

	//等待来自SIP进程的连接
	waitSIP();

	while(1);

	return 0;
}
