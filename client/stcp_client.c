#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "stcp_client.h"
#include "../common/constants.h"
#include "../topology/topology.h"

#define lprintf printf
#define VER_MAJOR 1
#define VER_MINER 14

typedef struct timespec timespec_t;
/*面向应用层的接口*/

/*List of TCB*/
client_tcb_t *TCB_list[MAX_TRANSPORT_CONNECTIONS];
pthread_mutex_t TCB_list_mutex;

/*List of cond*/
/*Provide timer for each TCB*/
pthread_cond_t *TimerCond_list[MAX_TRANSPORT_CONNECTIONS];
pthread_mutex_t TimerCond_list_mutex;

static int conn_sock;

//Declartion for funcitons
segBuf_t *sendBufDel(client_tcb_t *tcbP, segBuf_t *bufP);
client_tcb_t *get_tcb(int);

//
//  我们在下面提供了每个函数调用的原型定义和细节说明, 但这些只是指导性的, 你完全可以根据自己的想法来设计代码.
//
//  注意: 当实现这些函数时, 你需要考虑FSM中所有可能的状态, 这可以使用switch语句来实现.
//
//  目标: 你的任务就是设计并实现下面的函数原型.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
int tcp_output(int conn_sock, seg_t *seg)
{
	client_tcb_t *tcbP;
	tcbP=get_tcb(conn_sock);

	//check sum
	//send
	if(tcbP==NULL)
	{
		lprintf("Error get tcb!\n");
		return -1;
	}
	sip_sendseg(conn_sock,get_tcb(conn_sock)->server_nodeID, seg);

	return 1;
}
void sendBufFree(client_tcb_t *tcbP)
{
	segBuf_t *p;
	pthread_mutex_lock(tcbP->bufMutex);
	p=tcbP->sendBufHead;
	while(p!=NULL)
	{
		p=sendBufDel(tcbP, p);
	}

	tcbP->unAck_segNum=0;
	tcbP->sendBufHead=NULL;
	tcbP->sendBufTail=NULL;
	tcbP->sendBufunSent=NULL;

	pthread_mutex_unlock(tcbP->bufMutex);
}
segBuf_t* sendBufAdd(client_tcb_t *tcbP,  char *dataP, unsigned short int len)
{
	segBuf_t *p;
	//timespec_t now;
	int i;

	if(len==0)
	{
		return NULL;
	}

	/*
	if(tcbP->unAck_segNum > GBN_WINDOW)
	{
		lprintf("To many seg in send buf\n");
		return NULL;
	}
	*/

	if(len > MAX_SEG_LEN )
	{
		lprintf("Seg too big\n");
		return NULL;
	}

	p =  (segBuf_t *)malloc(sizeof(segBuf_t));
	p->seg.header.type = DATA;
	p->seg.header.src_port=tcbP->client_portNum;
	p->seg.header.dest_port=tcbP->server_portNum;
	p->seg.header.length=len;
	p->seg.header.ack_num=tcbP->ackSeqNum;
	p->seg.header.seq_num=tcbP->next_seqNum;
	tcbP->next_seqNum+=len;

	lprintf("Add seg_buf for PORT:%d SEQ:%d\n", tcbP->client_portNum, p->seg.header.seq_num);
	for(i=0;i<len;i++)
	{
		p->seg.data[i]=*dataP;
		dataP++;
	}

	p->sentTime.tv_sec = 0;
	p->sentTime.tv_usec = 0;
	p->next = NULL;

	pthread_mutex_lock(tcbP->bufMutex);
	if(tcbP->sendBufTail==NULL)
	{
		tcbP->sendBufHead=p;
		tcbP->sendBufTail=p;
	}
	else
	{
		tcbP->sendBufTail->next = p;
		tcbP->sendBufTail = p;
	}
	pthread_mutex_unlock(tcbP->bufMutex);

	return p;
}

/*This will del bufP from tcbP*/
/*Return next one */
segBuf_t *sendBufDel(client_tcb_t *tcbP, segBuf_t *bufP)
{
	segBuf_t *p, *pdel;

	p = tcbP->sendBufHead;

	if( (p == bufP) && (p!=NULL) )
	{
		tcbP->sendBufHead=p->next;
		lprintf("Del buf in PORT:%d, SEQ:%d\n",tcbP->client_portNum,bufP->seg.header.seq_num);
		free(p);
		return tcbP->sendBufHead;
	}

	while(p!=NULL)
	{
		if( p->next == bufP )
		{
			pdel=p->next;
			if(tcbP->sendBufTail==pdel)
			{
				tcbP->sendBufTail=p;
			}

			p->next=pdel->next;
			lprintf("Del buf in PORT:%d, SEQ:%d\n",tcbP->client_portNum,bufP->seg.header.seq_num);
			free(pdel);

			return p->next;
		}
		p=p->next;
	}
	return p;
}

client_tcb_t *get_tcb(int sock)
{
	if((sock>=0)&&(sock<MAX_TRANSPORT_CONNECTIONS))
		return TCB_list[sock];
	else
		return NULL;
}

int get_sock(unsigned int portNum,unsigned int src_nodeID)
{
	int i;

	for(i=0;i<MAX_TRANSPORT_CONNECTIONS;i++)
	{
		if( (TCB_list[i]!=NULL) && (TCB_list[i]->client_portNum==portNum)&& (TCB_list[i]->server_nodeID==src_nodeID))
		{
			return i;
		}
	}


	return -1;
}
// stcp客户端初始化
//
// 这个函数初始化TCB表, 将所有条目标记为NULL.
// 它还针对重叠网络TCP套接字描述符conn初始化一个STCP层的全局变量, 该变量作为sip_sendseg和sip_recvseg的输入参数.
// 最后, 这个函数启动seghandler线程来处理进入的STCP段. 客户端只有一个seghandler.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

void stcp_client_init(int conn) {
	int i;
	conn_sock=conn;
	pthread_t thr;

	lprintf("STCP Client %d.%d\n",VER_MAJOR,VER_MINER);
	lprintf("--------------------\n");
	pthread_mutex_init(&TCB_list_mutex, NULL);
	pthread_mutex_init(&TimerCond_list_mutex, NULL);
	for(i=0;i<MAX_TRANSPORT_CONNECTIONS;i++)
	{
		TCB_list[i] = NULL;
		TimerCond_list[i]=NULL;
	}

	printf("Create seg_handle\n");
	pthread_create(&thr,NULL,seghandler,(void *)0);
	return;
}

// 创建一个客户端TCB条目, 返回套接字描述符
//
// 这个函数查找客户端TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化. 例如, TCB state被设置为CLOSED，客户端端口被设置为函数调用参数client_port.
// TCB表中条目的索引号应作为客户端的新套接字ID被这个函数返回, 它用于标识客户端的连接.
// 如果TCB表中没有条目可用, 这个函数返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_sock(unsigned int client_port) {
	int i;
	int socket;
	int used;

	used=0;
	socket=-1;

	//Lock and update the TCB_List

	pthread_mutex_lock(&TCB_list_mutex);
	for(i=0;i<MAX_TRANSPORT_CONNECTIONS;i++)
	{
		if(TCB_list[i]==NULL)
		{
			if(socket<0)
			{
				socket=i;
			}
		}
		else if(TCB_list[i]->client_portNum == client_port)
		{
			used=1;
			break;
		}
	}

	if(used)
	{
	lprintf("Refuse request for PORT %d: PROT hab been used!\n",client_port);
		return -1;
	}

	i=socket;
	TCB_list[i]=(client_tcb_t*)malloc(sizeof(client_tcb_t));
	TCB_list[i]->client_portNum = client_port;
	TCB_list[i]->client_nodeID = getMyNodeID();
	TCB_list[i]->state = CLOSED;

	TCB_list[i]->bufMutex=(pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(TCB_list[i]->bufMutex, NULL);
	TCB_list[i]->senderMutex=(pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(TCB_list[i]->senderMutex,NULL);
	TCB_list[i]->timerMutex=(pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(TCB_list[i]->timerMutex,NULL);

	pthread_cond_init(&TCB_list[i]->senderCond,NULL);

	TCB_list[i]->sendBufHead=NULL;
	TCB_list[i]->sendBufTail=NULL;
	TCB_list[i]->sendBufunSent=NULL;

	TCB_list[i]->unAck_segNum=0;
	TCB_list[i]->next_seqNum=0;

	pthread_mutex_unlock(&TCB_list_mutex);

	pthread_mutex_lock(&TimerCond_list_mutex);
	TimerCond_list[i]=(pthread_cond_t *)malloc(sizeof(pthread_cond_t));
	pthread_cond_init(TimerCond_list[i],NULL);
	pthread_mutex_unlock(&TimerCond_list_mutex);

	lprintf("Get socket for PORT %d, %d\n",client_port,socket);
	return i;
}

// 连接STCP服务器
//
// 这个函数用于连接服务器. 它以套接字ID和服务器的端口号作为输入参数. 套接字ID用于找到TCB条目.
// 这个函数设置TCB的服务器端口号,  然后使用sip_sendseg()发送一个SYN段给服务器.
// 在发送了SYN段之后, 一个定时器被启动. 如果在SYNSEG_TIMEOUT时间之内没有收到SYNACK, SYN 段将被重传.
// 如果收到了, 就返回1. 否则, 如果重传SYN的次数大于SYN_MAX_RETRY, 就将state转换到CLOSED, 并返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_connect(int sockfd, unsigned int serverNodeID, unsigned int server_port) {
	seg_t seg;
	int i;

	if(TCB_list[sockfd]->state != CLOSED )
	{
		lprintf("Sockfd is not closed!\n");
		return -1;
	}

	pthread_mutex_lock(&TCB_list_mutex);
	if(TCB_list[sockfd]==NULL)
	{
		lprintf("Invalid socket\n");
		pthread_mutex_unlock(&TCB_list_mutex);
		return -1;
	}
	else
	{
		lprintf("Connecting...\n");
		TCB_list[sockfd]->server_portNum=server_port;
		TCB_list[sockfd]->server_nodeID=serverNodeID;
		TCB_list[sockfd]->state= SYNSENT;
		TCB_list[sockfd]->next_seqNum=0;

		seg.header.dest_port=TCB_list[sockfd]->server_portNum;
		seg.header.src_port=TCB_list[sockfd]->client_portNum;
		seg.header.seq_num=TCB_list[sockfd]->next_seqNum;
#ifdef COUNT_SIGN
		TCB_list[sockfd]->next_seqNum++;
#endif
		seg.header.ack_num=0;
		seg.header.type=SYN;
		seg.header.length=0;

		pthread_mutex_unlock(&TCB_list_mutex);

		for(i=0;i<SYN_MAX_RETRY;i++)
		{
			timespec_t wait_time;

			pthread_mutex_lock(&TimerCond_list_mutex);

			if(clock_gettime(CLOCK_REALTIME, &wait_time))
			{
				lprintf("Error get time!\n");
			}
			wait_time.tv_nsec+=SYN_TIMEOUT;
			wait_time.tv_sec+=wait_time.tv_nsec/1000000000;
			wait_time.tv_nsec=wait_time.tv_nsec%1000000000;

			sip_sendseg(conn_sock, TCB_list[sockfd]->server_nodeID,&seg);
			lprintf("Wait for SYNACK!\n");

			if(pthread_cond_timedwait(TimerCond_list[sockfd], &TimerCond_list_mutex, &wait_time))
			{
				pthread_mutex_unlock(&TimerCond_list_mutex);
				lprintf("Sync time out, retry %d/%d\n", i, SYN_MAX_RETRY);
			}
			else
			{
				pthread_mutex_unlock(&TimerCond_list_mutex);
				lprintf("Established!\n");
				return 0;
			}
		}
		lprintf("Max retry connect to the PORT %d, closed!\n", server_port);

		pthread_mutex_lock(&TCB_list_mutex);
		TCB_list[sockfd]->state = CLOSED;
		pthread_mutex_unlock(&TCB_list_mutex);
		return -1;
	}
}

void * resender(void* arg)
{
	client_tcb_t *tcbP;
	segBuf_t *bufP;
	//unsigned long delta;
	struct timeval wait_time;

	tcbP=(client_tcb_t*) arg;
	pthread_mutex_lock(tcbP->timerMutex);

	if(arg==NULL)
	{
			lprintf("Invalid tcb, resender not start\n");
			pthread_mutex_unlock(tcbP->timerMutex);
			return NULL;
	}

	while(1)
	{
		pthread_mutex_lock(tcbP->bufMutex);
		if(tcbP->unAck_segNum > 0)
		{
			pthread_mutex_unlock(tcbP->bufMutex);
			/*
			wait_time.tv_usec=SENDBUF_POLLING_INTERVAL % 1000000000 * 1000;
			wait_time.tv_sec=SENDBUF_POLLING_INTERVAL / 1000000000;
			*/
			wait_time.tv_usec=SENDBUF_POLLING_INTERVAL % 1000000000 / 1000;
			wait_time.tv_sec= SENDBUF_POLLING_INTERVAL / 1000000000;

			select(0, NULL, NULL, NULL, &wait_time);

			pthread_mutex_lock(tcbP->bufMutex);
			bufP = tcbP -> sendBufHead ;
			//lprintf("Start resend %d from %d\n", tcbP->unAck_segNum, tcbP->sendBufHead->seg.header.seq_num);
			while(bufP!=tcbP->sendBufunSent)
			{
					//lprintf("Resend buf SEQ:%d\n", bufP->seg.header.seq_num);
					sip_sendseg(conn_sock,tcbP->server_nodeID,&(bufP->seg));
					bufP=bufP->next;
			}
			pthread_mutex_unlock(tcbP->bufMutex);
		}
		else
		{
			pthread_mutex_unlock(tcbP->bufMutex);
			break;
		}
	}
	pthread_mutex_unlock(tcbP->timerMutex);
	lprintf("Resender complete\n");
	return NULL;
}
void * stcp_sender(void* arg)
{
	client_tcb_t *tcbP;

	tcbP=(client_tcb_t*) arg;
	pthread_detach(pthread_self());
	pthread_mutex_lock(tcbP->senderMutex);

	if(arg==NULL)
	{
			lprintf("Invalid tcb, sender not start\n");
			pthread_mutex_unlock(tcbP->senderMutex);
			return NULL;
	}

	while(tcbP->sendBufunSent!=NULL)
	{
		pthread_mutex_lock(tcbP->bufMutex);
		if(tcbP->unAck_segNum > GBN_WINDOW)
		{
			timespec_t wait_time;

			if(pthread_mutex_trylock( tcbP->timerMutex) == 0)
			{
					pthread_t temp;
					lprintf("Create resender for PORT:%d\n", tcbP->client_portNum);
					pthread_create(&temp, NULL, resender, tcbP);
					pthread_mutex_unlock( tcbP->timerMutex);
			}

			lprintf("Unack segments too many\n");

			if(clock_gettime(CLOCK_REALTIME, &wait_time))
			{
				lprintf("Error get time!\n");
			}
			wait_time.tv_nsec+=SYN_TIMEOUT;
			wait_time.tv_sec+=wait_time.tv_nsec/1000000000;
			wait_time.tv_nsec=wait_time.tv_nsec%1000000000;
			pthread_cond_timedwait(&tcbP->senderCond, tcbP->bufMutex,& wait_time);

			pthread_mutex_unlock(tcbP->bufMutex);
			continue;
		}
		pthread_mutex_unlock(tcbP->bufMutex);

		tcbP->unAck_segNum++;
		gettimeofday(&(tcbP->sendBufunSent->sentTime), NULL);
		lprintf("Send seg SEQ:%d\n",tcbP->sendBufunSent->seg.header.seq_num);
		sip_sendseg(conn_sock,tcbP->server_nodeID, &(tcbP->sendBufunSent->seg));
		tcbP->sendBufunSent=tcbP->sendBufunSent->next;
	}

	pthread_mutex_lock(tcbP->bufMutex);
	if(pthread_mutex_trylock( tcbP->timerMutex) == 0)
	{
			pthread_t temp;
			lprintf("Create resender for PORT:%d\n", tcbP->client_portNum);

			pthread_create(&temp, NULL, resender, tcbP);
			pthread_mutex_unlock( tcbP->timerMutex);
	}
	pthread_mutex_unlock(tcbP->bufMutex);
	lprintf("Send complete for PORT:%d\n", tcbP->client_portNum);
	pthread_mutex_unlock(tcbP->senderMutex);

	return (void*)0;
}
// 发送数据给STCP服务器
//
// 这个函数发送数据给STCP服务器. 你不需要在本实验中实现它。
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_send(int sockfd, void* data, unsigned int length) {
	client_tcb_t *tcbP;
	segBuf_t* bufP;

	pthread_mutex_lock(&TCB_list_mutex);
	tcbP=get_tcb(sockfd);
	if(tcbP==NULL)
	{
		pthread_mutex_unlock(&TCB_list_mutex);
		lprintf("Not such sockfd!!\n");
		return -1;
	}

	if(tcbP->state!=CONNECTED)
	{
		pthread_mutex_unlock(&TCB_list_mutex);
		lprintf("Sockfd not connected!!\n");
		return -1;
	}

	if(length<MAX_SEG_LEN)
	{
		bufP=sendBufAdd(tcbP, data, length);
	}
	else
	{
		bufP=sendBufAdd(tcbP, data, MAX_SEG_LEN);
		length-=MAX_SEG_LEN;
		data+=MAX_SEG_LEN;
		while(length >= MAX_SEG_LEN)
		{
			sendBufAdd(tcbP, data, MAX_SEG_LEN);
			length-=MAX_SEG_LEN;
			data+=MAX_SEG_LEN;
		}
		sendBufAdd(tcbP, data, length);

	}

	if(tcbP->sendBufunSent==NULL)
	{
			tcbP->sendBufunSent=bufP;
	}

	if( pthread_mutex_trylock(tcbP->senderMutex)==0 )
	{
		pthread_t temp_thread;
		lprintf("Create sender for PORT:%d\n", tcbP->client_portNum);
		pthread_create(&temp_thread, NULL, stcp_sender,(void*)tcbP);
		pthread_mutex_unlock(tcbP->senderMutex);
	}

	pthread_mutex_unlock(&TCB_list_mutex);

	return 0;
}

// 断开到STCP服务器的连接
//
// 这个函数用于断开到服务器的连接. 它以套接字ID作为输入参数. 套接字ID用于找到TCB表中的条目.
// 这个函数发送FIN segment给服务器. 在发送FIN之后, state将转换到FINWAIT, 并启动一个定时器.
// 如果在最终超时之前state转换到CLOSED, 则表明FINACK已被成功接收. 否则, 如果在经过FIN_MAX_RETRY次尝试之后,
// state仍然为FINWAIT, state将转换到CLOSED, 并返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_disconnect(int sockfd) {
	seg_t seg;
	int i;

	if(TCB_list[sockfd]->state != CONNECTED)
	{
		lprintf("Sockfd is not connected!\n");
		return -1;
	}

	pthread_mutex_lock(&TCB_list_mutex);
	if(TCB_list[sockfd]==NULL)
	{
		lprintf("Invalid socket\n");
		pthread_mutex_unlock(&TCB_list_mutex);
		return -1;
	}
	else
	{

		seg.header.dest_port=TCB_list[sockfd]->server_portNum;
		seg.header.src_port=TCB_list[sockfd]->client_portNum;
		seg.header.seq_num=TCB_list[sockfd]->next_seqNum;
#ifdef COUNT_SIGN
		TCB_list[sockfd]->next_seqNum++;
#endif
		seg.header.ack_num=TCB_list[sockfd]->ackSeqNum;
		seg.header.type=FIN;
		seg.header.length=0;

		pthread_mutex_unlock(&TCB_list_mutex);

		//wait for sender and resender
		pthread_mutex_lock(TCB_list[sockfd]->senderMutex);
		pthread_mutex_lock(TCB_list[sockfd]->timerMutex);

		pthread_mutex_lock(&TCB_list_mutex);
		TCB_list[sockfd]->state= FINWAIT;
		pthread_mutex_unlock(&TCB_list_mutex);

		pthread_mutex_unlock(TCB_list[sockfd]->timerMutex);
		pthread_mutex_unlock(TCB_list[sockfd]->senderMutex);

		for(i=0;i<FIN_MAX_RETRY;i++)
		{
			timespec_t wait_time;

			clock_gettime(CLOCK_REALTIME, &wait_time);
			wait_time.tv_nsec+=FIN_TIMEOUT;
			wait_time.tv_sec+=wait_time.tv_nsec/1000000000;
			wait_time.tv_nsec=wait_time.tv_nsec%1000000000;

			pthread_mutex_lock(&TimerCond_list_mutex);
			lprintf("Sent FIN to PORT %d\n", seg.header.dest_port);
			sip_sendseg(conn_sock, TCB_list[sockfd]->server_nodeID,&seg);
			if(pthread_cond_timedwait(TimerCond_list[sockfd], &TimerCond_list_mutex, &wait_time))
			{
				pthread_mutex_unlock(&TimerCond_list_mutex);
				lprintf("FIN time out, retry %d/%d\n", i, FIN_MAX_RETRY);
			}
			else
			{
				pthread_mutex_unlock(&TimerCond_list_mutex);
				lprintf("Closed!\n");
				return 0;
			}
		}
		lprintf("Max retry send fin to the PORT %d, closed!\n", seg.header.dest_port);
		pthread_mutex_lock(&TCB_list_mutex);
		pthread_mutex_lock(get_tcb(sockfd)->bufMutex);
		TCB_list[sockfd]->state = CLOSED;
		TCB_list[sockfd]->unAck_segNum=0;
		sendBufFree(get_tcb(sockfd));
		pthread_mutex_unlock(get_tcb(sockfd)->bufMutex);
		pthread_mutex_unlock(&TCB_list_mutex);
	}
	return -1;
}

// 关闭STCP客户
//
// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_close(int sockfd) {
	//send_buf_free
	//segBuf_t *p,*pnext;
	client_tcb_t *tcbP;

	pthread_mutex_lock(&TCB_list_mutex);
	if(TCB_list[sockfd]!=NULL)
	{
		tcbP=get_tcb(sockfd);

		tcbP->unAck_segNum=0;
		tcbP->sendBufunSent=NULL;

		pthread_mutex_lock(tcbP->timerMutex);
		pthread_mutex_lock(tcbP->senderMutex);
		free(tcbP->timerMutex);
		free(tcbP->senderMutex);
		free(tcbP->bufMutex);

		while(sendBufDel(tcbP,tcbP->sendBufHead)!=NULL);
		free(TCB_list[sockfd]);
		pthread_mutex_unlock(&TCB_list_mutex);
	}
	else
	{
		pthread_mutex_unlock(&TCB_list_mutex);
		return -1;
	}
	return 0;
}

// 处理进入段的线程
//
// 这是由stcp_client_init()启动的线程. 它处理所有来自服务器的进入段.
// seghandler被设计为一个调用sip_recvseg()的无穷循环. 如果sip_recvseg()失败, 则说明重叠网络连接已关闭,
// 线程将终止. 根据STCP段到达时连接所处的状态, 可以采取不同的动作. 请查看客户端FSM以了解更多细节.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

void *seghandler(void* arg) {
	seg_t seg_buf;
	client_tcb_t *p;
	//client_tcb_t *tcbP;
	segBuf_t *bufP;
	int sock;
	int src_nodeID;

	pthread_detach(pthread_self());
	lprintf("Wait for seg!\n");
	while(1)
	{
		if(-1==sip_recvseg(conn_sock,&src_nodeID, &seg_buf)) continue;


		pthread_mutex_lock(&TCB_list_mutex);
		sock=get_sock(seg_buf.header.dest_port, src_nodeID);
		p=get_tcb(sock);

		if(p==NULL)
		{
			pthread_mutex_unlock(&TCB_list_mutex);
			lprintf("Discard: Port not used!\n");
			continue;
		}


		if(seg_buf.header.src_port!=p->server_portNum)
		{
			pthread_mutex_unlock(&TCB_list_mutex);
			lprintf("Discard: Port not connected to this server!\n");
			continue;
		}

		/*
		if(seg_buf.header.ack_num!=p->next_seqNum)
		{
			pthread_mutex_unlock(&TCB_list_mutex);
			lprintf("Discard: Unexpected ack num!\n");
			continue;

		}
		*/
		pthread_mutex_unlock(&TCB_list_mutex);

		switch(p->state)
		{
			case CLOSED:
				//Do nothing
				break;
			case SYNSENT:
				if(seg_buf.header.type == SYNACK)
				{
					pthread_mutex_unlock(&TCB_list_mutex);
					p->state=CONNECTED;
#ifdef COUNT_SIGN
					p->ackSeqNum=seg_buf.header.ack_num+1;
#endif
					pthread_mutex_unlock(&TCB_list_mutex);

					pthread_mutex_lock(&TimerCond_list_mutex);
					pthread_cond_signal(TimerCond_list[sock]);
					pthread_mutex_unlock(&TimerCond_list_mutex);
				}
				break;
		       	case CONNECTED:
				if(seg_buf.header.type == DATAACK)
				{
					pthread_mutex_lock(&TCB_list_mutex);
					pthread_mutex_lock(p->bufMutex);

					bufP=p->sendBufHead;
					lprintf("Compare SEQ:%d\n", seg_buf.header.seq_num);
					while(bufP!=NULL)
					{
							if(bufP->seg.header.seq_num < seg_buf.header.ack_num)
							{
									p->unAck_segNum--;
									bufP=sendBufDel(p,bufP);
									continue;
							}
							bufP=bufP->next;
					}

					pthread_mutex_unlock(p->bufMutex);
					pthread_mutex_unlock(&TCB_list_mutex);
					pthread_cond_signal(&p->senderCond);
				}
				break;
			case FINWAIT:
				if(seg_buf.header.type == FINACK)
				{
					pthread_mutex_unlock(&TCB_list_mutex);
					p->state=CLOSED;
#ifdef COUNT_SIGN
					p->ackSeqNum=seg_buf.header.ack_num+1;
#endif
					pthread_mutex_unlock(&TCB_list_mutex);

					pthread_mutex_lock(&TimerCond_list_mutex);
					pthread_cond_signal(TimerCond_list[sock]);
					pthread_mutex_unlock(&TimerCond_list_mutex);
				}
				break;
		}
	}

	return NULL;
}



