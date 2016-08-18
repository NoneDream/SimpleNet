// 文件名 pkt.c
// 创建日期: 2015年

#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

#include "pkt.h"
#include "../topology/topology.h"

#define nDEBUG

static const char flag_head[2]={'!','&'},flag_end[2]={'!','#'};
pthread_mutex_t son_send_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t forward_mutex=PTHREAD_MUTEX_INITIALIZER;

// son_sendpkt()由SIP进程调用, 其作用是要求SON进程将报文发送到重叠网络中. SON进程和SIP进程通过一个本地TCP连接互连.
// 在son_sendpkt()中, 报文及其下一跳的节点ID被封装进数据结构sendpkt_arg_t, 并通过TCP连接发送给SON进程.
// 参数son_conn是SIP进程和SON进程之间的TCP连接套接字描述符.
// 当通过SIP进程和SON进程之间的TCP连接发送数据结构sendpkt_arg_t时, 使用'!&'和'!#'作为分隔符, 按照'!& sendpkt_arg_t结构 !#'的顺序发送.
// 如果发送成功, 返回1, 否则返回-1.
int son_sendpkt(int nextNodeID, sip_pkt_t* pkt, int son_conn)
{
    int nbrNum,i;
    int *nbrIDs;

    if(nextNodeID==BROADCAST_NODEID){
        nbrNum=topology_getNbrNum();
        nbrIDs=topology_getNbrArray();
        for(i=0;i<nbrNum;++i){
            son_sendpkt(nbrIDs[i],pkt,son_conn);
        }
        free(nbrIDs);
        return 1;
    }

    while(pthread_mutex_trylock(&son_send_mutex))usleep(10);////////////////////////////////////

    if(2!=send(son_conn, flag_head,2, 0)){
        pthread_mutex_unlock(&son_send_mutex);
        return -1;
    }
    if(sizeof(int)!=send(son_conn, &nextNodeID,sizeof(int), 0)){
        pthread_mutex_unlock(&son_send_mutex);
        return -1;
    }
    if((pkt->header.length+sizeof(sip_hdr_t))!=send(son_conn, pkt,pkt->header.length+sizeof(sip_hdr_t), 0)){
        pthread_mutex_unlock(&son_send_mutex);
        return -1;
    }
    if(2!=send(son_conn, flag_end,2, 0)){
        pthread_mutex_unlock(&son_send_mutex);
        return -1;
    }

    pthread_mutex_unlock(&son_send_mutex);
#ifdef DEBUG
    printf("[SIP]\t[Sendto]\t[SON]\n");
#endif
    return 1;
}

// son_recvpkt()函数由SIP进程调用, 其作用是接收来自SON进程的报文.
// 参数son_conn是SIP进程和SON进程之间TCP连接的套接字描述符. 报文通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#.
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点
// PKTSTART2 -- 接收到'!', 期待'&'
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收
// 如果成功接收报文, 返回1, 否则返回-1.
int son_recvpkt(sip_pkt_t* pkt, int son_conn)
{
    char check[3];

    check[2]=0;
    while(strcmp(check,"!&"))
    {
        check[0]=check[1];
        recv(son_conn, check+1, 1, MSG_WAITALL);
    }
    recv(son_conn,&pkt->header,sizeof(sip_hdr_t),MSG_WAITALL);
    recv(son_conn,pkt->data,pkt->header.length,MSG_WAITALL);
    recv(son_conn, check, 2, MSG_WAITALL);
    if(strcmp(check,"!#"))return -1;
    else {
#ifdef DEBUG
        printf("[SIP]\t[Recvfrom]\t[son]\n");
#endif
        return 1;
    }
}

// 这个函数由SON进程调用, 其作用是接收数据结构sendpkt_arg_t.
// 报文和下一跳的节点ID被封装进sendpkt_arg_t结构.
// 参数sip_conn是在SIP进程和SON进程之间的TCP连接的套接字描述符.
// sendpkt_arg_t结构通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#.
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点
// PKTSTART2 -- 接收到'!', 期待'&'
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收
// 如果成功接收sendpkt_arg_t结构, 返回1, 否则返回-1.
int getpktToSend(sip_pkt_t* pkt, int* nextNode,int sip_conn)
{
    char check[3];

    check[2]=0;
    while(strcmp(check,"!&"))
    {
        check[0]=check[1];
        recv(sip_conn, check+1, 1, MSG_WAITALL);
    }
    recv(sip_conn, nextNode,sizeof(int), MSG_WAITALL);
    recv(sip_conn,&pkt->header,sizeof(sip_hdr_t),MSG_WAITALL);
    recv(sip_conn,pkt->data,pkt->header.length,MSG_WAITALL);
    recv(sip_conn, check, 2, 0);
    if(strcmp(check,"!#"))return -1;
    else {
#ifdef DEBUG
        printf("[SON]\t[Recvfrom]\t[SIP]\n");
#endif
        return 1;
    }
}

// forwardpktToSIP()函数是在SON进程接收到来自重叠网络中其邻居的报文后被调用的.
// SON进程调用这个函数将报文转发给SIP进程.
// 参数sip_conn是SIP进程和SON进程之间的TCP连接的套接字描述符.
// 报文通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#, 按照'!& 报文 !#'的顺序发送.
// 如果报文发送成功, 返回1, 否则返回-1.
int forwardpktToSIP(sip_pkt_t* pkt, int sip_conn)
{
    while(pthread_mutex_trylock(&forward_mutex))usleep(10);
    if(2!=send(sip_conn, flag_head,2, 0)){
        pthread_mutex_unlock(&forward_mutex);
        return -1;
    }
    if((pkt->header.length+sizeof(sip_hdr_t))!=send(sip_conn, pkt,pkt->header.length+sizeof(sip_hdr_t), 0)){
        pthread_mutex_unlock(&forward_mutex);
        return -1;
    }
    if(2!=send(sip_conn, flag_end,2, 0)){
        pthread_mutex_unlock(&forward_mutex);
        return -1;
    }
#ifdef DEBUG
    printf("[SON]\t[Sendto]\t[SIP]\n");
#endif
    pthread_mutex_unlock(&forward_mutex);
    return 1;
}

// sendpkt()函数由SON进程调用, 其作用是将接收自SIP进程的报文发送给下一跳.
// 参数conn是到下一跳节点的TCP连接的套接字描述符.
// 报文通过SON进程和其邻居节点之间的TCP连接发送, 使用分隔符!&和!#, 按照'!& 报文 !#'的顺序发送.
// 如果报文发送成功, 返回1, 否则返回-1.
int sendpkt(sip_pkt_t* pkt, int conn)
{
    if(2!=send(conn, flag_head,2, 0))return -1;
    if((pkt->header.length+sizeof(sip_hdr_t))!=send(conn, pkt,pkt->header.length+sizeof(sip_hdr_t), 0))return -1;
    if(2!=send(conn, flag_end,2, 0))return -1;
#ifdef DEBUG
    printf("[SON]\t[Sendto]\t[Nbr]\n");
#endif
    return 1;
}

// recvpkt()函数由SON进程调用, 其作用是接收来自重叠网络中其邻居的报文.
// 参数conn是到其邻居的TCP连接的套接字描述符.
// 报文通过SON进程和其邻居之间的TCP连接发送, 使用分隔符!&和!#.
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点
// PKTSTART2 -- 接收到'!', 期待'&'
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收
// 如果成功接收报文, 返回1, 否则返回-1.
int recvpkt(sip_pkt_t* pkt, int conn)
{
    char check[3];

    check[2]=0;
    while(strcmp(check,"!&"))
    {
        check[0]=check[1];
        recv(conn, check+1, 1, MSG_WAITALL);
    }
    recv(conn,&pkt->header,sizeof(sip_hdr_t),MSG_WAITALL);
    recv(conn,pkt->data,pkt->header.length,MSG_WAITALL);
    recv(conn, check, 2, MSG_WAITALL);
    if(strcmp(check,"!#"))return -1;
    else {
#ifdef DEBUG
        printf("[SON]\t[Recvfrom]\t[Nbr]\n");
#endif
        return 1;
    }
}
