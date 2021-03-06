//文件名: server/stcp_server.c
//
//描述: 这个文件包含STCP服务器接口实现.
//
//创建日期: 2015年

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/select.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "stcp_server.h"
#include "stcp_buffer.h"
#include "../topology/topology.h"
#include "../common/constants.h"

#define DEBUG_SER

//声明tcbtable为全局变量
server_tcb_t* tcb_list[MAX_TRANSPORT_CONNECTIONS];
//声明到SIP进程的连接为全局变量
int sip_conn;
pthread_t thr_timeout[MAX_TRANSPORT_CONNECTIONS];
int tcp_socket;
pthread_t thr_seghandler;

/*********************************************************************/
//
//STCP API实现
//
/*********************************************************************/

// 这个函数初始化TCB表, 将所有条目标记为NULL. 它还针对TCP套接字描述符conn初始化一个STCP层的全局变量,
// 该变量作为sip_sendseg和sip_recvseg的输入参数. 最后, 这个函数启动seghandler线程来处理进入的STCP段.
// 服务器只有一个seghandler.
void stcp_server_init(int conn) {
	int count=0;

    printf("STCP_SERVER 1.0\n");
    printf("-------------------------------\n");
	//初始化TCB列表
	while(count++<MAX_TRANSPORT_CONNECTIONS)
	{
		tcb_list[count]=NULL;
	}

	tcp_socket=conn;

	pthread_create(&thr_seghandler,NULL,seghandler,(void *)0);

	return;
}

// 这个函数查找服务器TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化, 例如, TCB state被设置为CLOSED, 服务器端口被设置为函数调用参数server_port.
// TCB表中条目的索引应作为服务器的新套接字ID被这个函数返回, 它用于标识服务器端的连接.
// 如果TCB表中没有条目可用, 这个函数返回-1.
int stcp_server_sock(unsigned int server_port) {
	int count=0;
	server_tcb_t *current_tcb;

	while(count<MAX_TRANSPORT_CONNECTIONS){
		if(tcb_list[count]==NULL)break;
		++count;
	}

	if(count==MAX_TRANSPORT_CONNECTIONS)return -1;

	tcb_list[count]=malloc(sizeof(server_tcb_t));
	current_tcb=tcb_list[count];
	current_tcb->server_nodeID=topology_getMyNodeID();
	current_tcb->server_portNum=server_port;
	current_tcb->client_nodeID=-1;
	current_tcb->client_portNum=-1;
	current_tcb->state=CLOSED;
	current_tcb->expect_seqNum=0;
	buffer_init(&current_tcb->recvBuf);
	current_tcb->usedBufLen=0;
	current_tcb->bufMutex=malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(current_tcb->bufMutex,NULL);
/*
typedef struct server_tcb {
	unsigned int server_nodeID;        //服务器节点ID, 类似IP地址
	unsigned int server_portNum;       //服务器端口号
	unsigned int client_nodeID;     //客户端节点ID, 类似IP地址
	unsigned int client_portNum;    //客户端端口号
	unsigned int state;         	//服务器状态
	unsigned int expect_seqNum;     //服务器期待的数据序号
	char* recvBuf;                  //指向接收缓冲区的指针
	unsigned int  usedBufLen;       //接收缓冲区中已接收数据的大小
	pthread_mutex_t* bufMutex;      //指向一个互斥量的指针, 该互斥量用于对接收缓冲区的访问
} server_tcb_t;
*/
	return count;
}

// 这个函数使用sockfd获得TCB指针, 并将TCB复制一份,连接的state转换为LISTENING. 它然后启动定时器进入忙等待直到TCB状态转换为CONNECTED
// (当收到SYN时, seghandler会进行状态的转换). 该函数在一个无穷循环中等待TCB的state转换为CONNECTED,
// 当发生了转换时, 该函数返回新的socket. 你可以使用不同的方法来实现这种阻塞等待.
int stcp_server_accept(int sockfd) {
    int new_fd=0;
	server_tcb_t *current_tcb;

    if(tcb_list[sockfd]==NULL)return -1;

	while(new_fd<MAX_TRANSPORT_CONNECTIONS){
		if(tcb_list[new_fd]==NULL)break;
		++new_fd;
	}

	if(new_fd==MAX_TRANSPORT_CONNECTIONS)return -1;

	tcb_list[new_fd]=malloc(sizeof(server_tcb_t));
	current_tcb=tcb_list[new_fd];
    //新TCB的初始化
	memcpy(current_tcb,tcb_list[sockfd],sizeof(server_tcb_t));

	buffer_init(&current_tcb->recvBuf);
	current_tcb->bufMutex=malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(current_tcb->bufMutex,NULL);
    //新TCB状态转换为LISTENING
	tcb_list[new_fd]->state=LISTENING;
    //等待
    while(tcb_list[new_fd]->state!=CONNECTED)usleep(10000);

	return new_fd;
}

// 接收来自STCP客户端的数据. 这个函数每隔RECVBUF_POLLING_INTERVAL时间
// 就查询接收缓冲区, 直到等待的数据到达, 它然后存储数据并返回1. 如果这个函数失败, 则返回-1.
int stcp_server_recv(int sockfd, void* buf, unsigned int len){
    int maxatime=BUFSIZE-MAX_SEG_LEN-20;
    int length;
    if(len>maxatime)length=maxatime;
    else length=len;
    while((tcb_list[sockfd]->recvBuf.usedlen)<length){
        if(tcb_list[sockfd]->state!=CONNECTED)return -1;
        sleep(RECVBUF_POLLING_INTERVAL);
    }
    //while(pthread_mutex_trylock(tcb_list[sockfd]->bufMutex))usleep(20);
    out(&tcb_list[sockfd]->recvBuf,buf,length);
    //pthread_mutex_unlock(tcb_list[sockfd]->bufMutex);
    if(len>maxatime){
    #ifdef DEBUG_SER
        printf("[Buffer]Too long start next(maxatime:%d,%d bytes left).\n",maxatime,len-maxatime);
    #endif
        stcp_server_recv(sockfd,buf+maxatime,len-maxatime);
    }
    return 1;
}

// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
int stcp_server_close(int sockfd) {
    if(tcb_list[sockfd]==NULL)return -1;

    if(tcb_list[sockfd]->state==CLOSED){
        printf("Already disconnected release TCB.....\n");
    }
    else if(tcb_list[sockfd]->state==CLOSEWAIT){
        printf("Wait for close.....\n");
        pthread_join(thr_timeout[sockfd],NULL);
        printf("Closed\n");
    }
    else {
        printf("Current state is:%d,can't disconnect.\n",tcb_list[sockfd]->state);
        return -1;
    }
    pthread_mutex_destroy(tcb_list[sockfd]->bufMutex);
    free(tcb_list[sockfd]->bufMutex);
    free(tcb_list[sockfd]->recvBuf.p_const);
    free(tcb_list[sockfd]);
    tcb_list[sockfd]=NULL;

    return 1;
}

// 这是由stcp_server_init()启动的线程. 它处理所有来自客户端的进入数据. seghandler被设计为一个调用sip_recvseg()的无穷循环,
// 如果sip_recvseg()失败, 则说明到SIP进程的连接已关闭, 线程将终止. 根据STCP段到达时连接所处的状态, 可以采取不同的动作.
// 请查看服务端FSM以了解更多细节.
void *seghandler(void* arg) {
    seg_t recv_buf,send_buf;
    int sock,ID;
    server_tcb_t *current_tcb;

    while(1){
        if(-1==sip_recvseg(tcp_socket,&ID,&recv_buf))continue;
        printf("[SIP]->[STCP]\t");
        //找到段对应的TCB（根据端口号）
        for(sock=0;sock<MAX_TRANSPORT_CONNECTIONS;++sock){
            if(tcb_list[sock]==NULL){
                continue;
            }
            if(tcb_list[sock]->server_portNum==recv_buf.header.dest_port&&tcb_list[sock]->client_portNum==recv_buf.header.src_port&&ID==tcb_list[sock]->client_nodeID)break;
            if(recv_buf.header.type==SYN){
                if(tcb_list[sock]->server_portNum==recv_buf.header.dest_port&&tcb_list[sock]->state==LISTENING)break;
            }
        }
        if(sock==MAX_TRANSPORT_CONNECTIONS){
		printf("Can't find socket!\n");
		continue;
	}
        else current_tcb=tcb_list[sock];
        switch(current_tcb->state){
            case CLOSED:{
		printf("Error:current state is CLOSED!\n");
                break;
            }
            case LISTENING:{
                if(recv_buf.header.type==SYN){
                    send_buf.header.type=SYNACK;
                    send_buf.header.src_port=recv_buf.header.dest_port;
                    send_buf.header.length=0;
                    send_buf.header.dest_port=recv_buf.header.src_port;
                    sip_sendseg(tcp_socket,ID,&send_buf);
                    current_tcb->client_portNum=recv_buf.header.src_port;
                    current_tcb->client_nodeID=ID;
                    current_tcb->state=CONNECTED;
#ifdef DEBUG_SER
                    printf("[LISTENING] Receive SYN [CONNECTED]\n");
#endif
                }
		else printf("Error:current state is LISTENING!\n");
                break;
            }
            case CONNECTED:{
                if(recv_buf.header.type==SYN){
                    send_buf.header.type=SYNACK;
                    send_buf.header.src_port=recv_buf.header.dest_port;
                    send_buf.header.length=0;
                    send_buf.header.dest_port=recv_buf.header.src_port;
                    sip_sendseg(tcp_socket,ID,&send_buf);
		    printf("[CONNECTED] Receive SYN.\n");
                    //current_tcb->state=CONNECTED;
                }
                else if(recv_buf.header.type==DATA){
                    if(recv_buf.header.seq_num>current_tcb->expect_seqNum){
                        printf("[CONNECTED] Receive nouse DATA.\n");
                        break;
                    }
                    if(recv_buf.header.seq_num==current_tcb->expect_seqNum){
                        if(iffull(&current_tcb->recvBuf,recv_buf.header.length)){
#ifdef DEBUG_SER
                            printf("[CONNECTED] Buffer full,can't store.\n");
#endif
                            break;
                        }
                        //if(pthread_mutex_trylock(current_tcb->bufMutex))break;
                        if(in(&current_tcb->recvBuf,recv_buf.data,recv_buf.header.length)){//数据放入缓存
                            printf("[CONNECTED] Receive DATA,but can't store to buffer.\n");
                            break;
                        }
                        //pthread_mutex_unlock(current_tcb->bufMutex);
                        current_tcb->expect_seqNum+=recv_buf.header.length;
#ifdef DEBUG_SER
                        printf("[CONNECTED] Receive DATA,store to buffer.\n");
#endif
                    }
#ifdef DEBUG_SER
                    else printf("[CONNECTED] Receive no_use DATA.\n");
#endif
                    send_buf.header.type=DATAACK;
                    send_buf.header.src_port=recv_buf.header.dest_port;
                    send_buf.header.length=0;
                    send_buf.header.dest_port=recv_buf.header.src_port;
                    send_buf.header.ack_num=current_tcb->expect_seqNum;
                    sip_sendseg(tcp_socket,ID,&send_buf);//发送确认

		    //printf("[Send]Pretend to send a ack %d.\n",send_buf.header.ack_num);
                }
                else if(recv_buf.header.type==FIN){
                    send_buf.header.type=FINACK;
                    send_buf.header.src_port=recv_buf.header.dest_port;
                    send_buf.header.length=0;
                    send_buf.header.dest_port=recv_buf.header.src_port;
                    sip_sendseg(tcp_socket,ID,&send_buf);
		    //printf("[Send]Pretend to send a ack %d.\n",send_buf.header.ack_num);
                    pthread_create(&thr_timeout[sock],NULL,close_timeout,(void *)current_tcb);
                    current_tcb->state=CLOSEWAIT;
#ifdef DEBUG_SER
                    printf("[CONNECTED] Receive FIN [CLOSEWAIT]\n");
#endif
                }
		else printf("Error:unknown type!\n");
                break;
            }
            case CLOSEWAIT:{
                if(recv_buf.header.type==FIN){
                    send_buf.header.type=FINACK;
                    send_buf.header.src_port=recv_buf.header.dest_port;
                    send_buf.header.length=0;
                    send_buf.header.dest_port=recv_buf.header.src_port;
                    sip_sendseg(tcp_socket,ID,&send_buf);
#ifdef DEBUG_SER
                    printf("[CLOSEWAIT] Receive no_use FIN.\n");
#endif
                    //current_tcb->state=CLOSEWAIT;
                }
                break;
            }
            default:break;
        }
    }

    return 0;
}

void* close_timeout(void* arg){
    sleep(CLOSEWAIT_TIMEOUT);
    ((server_tcb_t *)arg)->state=CLOSED;
#ifdef DEBUG_SER
    printf("[CLOSEWAIT] Closewait timeout [CLOSED]\n");
#endif
    return 0;
}

