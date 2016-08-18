//�ļ���: server/stcp_server.c
//
//����: ����ļ�����STCP�������ӿ�ʵ��.
//
//��������: 2015��

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

//����tcbtableΪȫ�ֱ���
server_tcb_t* tcb_list[MAX_TRANSPORT_CONNECTIONS];
//������SIP���̵�����Ϊȫ�ֱ���
int sip_conn;
pthread_t thr_timeout[MAX_TRANSPORT_CONNECTIONS];
int tcp_socket;
pthread_t thr_seghandler;

/*********************************************************************/
//
//STCP APIʵ��
//
/*********************************************************************/

// ���������ʼ��TCB��, ��������Ŀ���ΪNULL. �������TCP�׽���������conn��ʼ��һ��STCP���ȫ�ֱ���,
// �ñ�����Ϊsip_sendseg��sip_recvseg���������. ���, �����������seghandler�߳������������STCP��.
// ������ֻ��һ��seghandler.
void stcp_server_init(int conn) {
	int count=0;

    printf("STCP_SERVER 1.0\n");
    printf("-------------------------------\n");
	//��ʼ��TCB�б�
	while(count++<MAX_TRANSPORT_CONNECTIONS)
	{
		tcb_list[count]=NULL;
	}

	tcp_socket=conn;

	pthread_create(&thr_seghandler,NULL,seghandler,(void *)0);

	return;
}

// ����������ҷ�����TCB�����ҵ���һ��NULL��Ŀ, Ȼ��ʹ��malloc()Ϊ����Ŀ����һ���µ�TCB��Ŀ.
// ��TCB�е������ֶζ�����ʼ��, ����, TCB state������ΪCLOSED, �������˿ڱ�����Ϊ�������ò���server_port.
// TCB������Ŀ������Ӧ��Ϊ�����������׽���ID�������������, �����ڱ�ʶ�������˵�����.
// ���TCB����û����Ŀ����, �����������-1.
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
	unsigned int server_nodeID;        //�������ڵ�ID, ����IP��ַ
	unsigned int server_portNum;       //�������˿ں�
	unsigned int client_nodeID;     //�ͻ��˽ڵ�ID, ����IP��ַ
	unsigned int client_portNum;    //�ͻ��˶˿ں�
	unsigned int state;         	//������״̬
	unsigned int expect_seqNum;     //�������ڴ����������
	char* recvBuf;                  //ָ����ջ�������ָ��
	unsigned int  usedBufLen;       //���ջ��������ѽ������ݵĴ�С
	pthread_mutex_t* bufMutex;      //ָ��һ����������ָ��, �û��������ڶԽ��ջ������ķ���
} server_tcb_t;
*/
	return count;
}

// �������ʹ��sockfd���TCBָ��, ����TCB����һ��,���ӵ�stateת��ΪLISTENING. ��Ȼ��������ʱ������æ�ȴ�ֱ��TCB״̬ת��ΪCONNECTED
// (���յ�SYNʱ, seghandler�����״̬��ת��). �ú�����һ������ѭ���еȴ�TCB��stateת��ΪCONNECTED,
// ��������ת��ʱ, �ú��������µ�socket. �����ʹ�ò�ͬ�ķ�����ʵ�����������ȴ�.
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
    //��TCB�ĳ�ʼ��
	memcpy(current_tcb,tcb_list[sockfd],sizeof(server_tcb_t));

	buffer_init(&current_tcb->recvBuf);
	current_tcb->bufMutex=malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(current_tcb->bufMutex,NULL);
    //��TCB״̬ת��ΪLISTENING
	tcb_list[new_fd]->state=LISTENING;
    //�ȴ�
    while(tcb_list[new_fd]->state!=CONNECTED)usleep(10000);

	return new_fd;
}

// ��������STCP�ͻ��˵�����. �������ÿ��RECVBUF_POLLING_INTERVALʱ��
// �Ͳ�ѯ���ջ�����, ֱ���ȴ������ݵ���, ��Ȼ��洢���ݲ�����1. ����������ʧ��, �򷵻�-1.
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

// �����������free()�ͷ�TCB��Ŀ. ��������Ŀ���ΪNULL, �ɹ�ʱ(��λ����ȷ��״̬)����1,
// ʧ��ʱ(��λ�ڴ����״̬)����-1.
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

// ������stcp_server_init()�������߳�. �������������Կͻ��˵Ľ�������. seghandler�����Ϊһ������sip_recvseg()������ѭ��,
// ���sip_recvseg()ʧ��, ��˵����SIP���̵������ѹر�, �߳̽���ֹ. ����STCP�ε���ʱ����������״̬, ���Բ�ȡ��ͬ�Ķ���.
// ��鿴�����FSM���˽����ϸ��.
void *seghandler(void* arg) {
    seg_t recv_buf,send_buf;
    int sock,ID;
    server_tcb_t *current_tcb;

    while(1){
        if(-1==sip_recvseg(tcp_socket,&ID,&recv_buf))continue;
        printf("[SIP]->[STCP]\t");
        //�ҵ��ζ�Ӧ��TCB�����ݶ˿ںţ�
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
                        if(in(&current_tcb->recvBuf,recv_buf.data,recv_buf.header.length)){//���ݷ��뻺��
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
                    sip_sendseg(tcp_socket,ID,&send_buf);//����ȷ��

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
