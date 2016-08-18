// �ļ��� pkt.c
// ��������: 2015��

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

// son_sendpkt()��SIP���̵���, ��������Ҫ��SON���̽����ķ��͵��ص�������. SON���̺�SIP����ͨ��һ������TCP���ӻ���.
// ��son_sendpkt()��, ���ļ�����һ���Ľڵ�ID����װ�����ݽṹsendpkt_arg_t, ��ͨ��TCP���ӷ��͸�SON����.
// ����son_conn��SIP���̺�SON����֮���TCP�����׽���������.
// ��ͨ��SIP���̺�SON����֮���TCP���ӷ������ݽṹsendpkt_arg_tʱ, ʹ��'!&'��'!#'��Ϊ�ָ���, ����'!& sendpkt_arg_t�ṹ !#'��˳����.
// ������ͳɹ�, ����1, ���򷵻�-1.
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

// son_recvpkt()������SIP���̵���, �������ǽ�������SON���̵ı���.
// ����son_conn��SIP���̺�SON����֮��TCP���ӵ��׽���������. ����ͨ��SIP���̺�SON����֮���TCP���ӷ���, ʹ�÷ָ���!&��!#.
// Ϊ�˽��ձ���, �������ʹ��һ���򵥵�����״̬��FSM
// PKTSTART1 -- ���
// PKTSTART2 -- ���յ�'!', �ڴ�'&'
// PKTRECV -- ���յ�'&', ��ʼ��������
// PKTSTOP1 -- ���յ�'!', �ڴ�'#'�Խ������ݵĽ���
// ����ɹ����ձ���, ����1, ���򷵻�-1.
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

// ���������SON���̵���, �������ǽ������ݽṹsendpkt_arg_t.
// ���ĺ���һ���Ľڵ�ID����װ��sendpkt_arg_t�ṹ.
// ����sip_conn����SIP���̺�SON����֮���TCP���ӵ��׽���������.
// sendpkt_arg_t�ṹͨ��SIP���̺�SON����֮���TCP���ӷ���, ʹ�÷ָ���!&��!#.
// Ϊ�˽��ձ���, �������ʹ��һ���򵥵�����״̬��FSM
// PKTSTART1 -- ���
// PKTSTART2 -- ���յ�'!', �ڴ�'&'
// PKTRECV -- ���յ�'&', ��ʼ��������
// PKTSTOP1 -- ���յ�'!', �ڴ�'#'�Խ������ݵĽ���
// ����ɹ�����sendpkt_arg_t�ṹ, ����1, ���򷵻�-1.
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

// forwardpktToSIP()��������SON���̽��յ������ص����������ھӵı��ĺ󱻵��õ�.
// SON���̵����������������ת����SIP����.
// ����sip_conn��SIP���̺�SON����֮���TCP���ӵ��׽���������.
// ����ͨ��SIP���̺�SON����֮���TCP���ӷ���, ʹ�÷ָ���!&��!#, ����'!& ���� !#'��˳����.
// ������ķ��ͳɹ�, ����1, ���򷵻�-1.
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

// sendpkt()������SON���̵���, �������ǽ�������SIP���̵ı��ķ��͸���һ��.
// ����conn�ǵ���һ���ڵ��TCP���ӵ��׽���������.
// ����ͨ��SON���̺����ھӽڵ�֮���TCP���ӷ���, ʹ�÷ָ���!&��!#, ����'!& ���� !#'��˳����.
// ������ķ��ͳɹ�, ����1, ���򷵻�-1.
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

// recvpkt()������SON���̵���, �������ǽ��������ص����������ھӵı���.
// ����conn�ǵ����ھӵ�TCP���ӵ��׽���������.
// ����ͨ��SON���̺����ھ�֮���TCP���ӷ���, ʹ�÷ָ���!&��!#.
// Ϊ�˽��ձ���, �������ʹ��һ���򵥵�����״̬��FSM
// PKTSTART1 -- ���
// PKTSTART2 -- ���յ�'!', �ڴ�'&'
// PKTRECV -- ���յ�'&', ��ʼ��������
// PKTSTOP1 -- ���յ�'!', �ڴ�'#'�Խ������ݵĽ���
// ����ɹ����ձ���, ����1, ���򷵻�-1.
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
