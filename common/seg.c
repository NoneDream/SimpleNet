#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "seg.h"
#include "stdio.h"

#define nDEBUG_SEG

static const char flag_head[2]={'!','&'},flag_end[2]={'!','#'};
pthread_mutex_t sip_send_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t stcp_send_mutex=PTHREAD_MUTEX_INITIALIZER;

//STCP进程使用这个函数发送sendseg_arg_t结构(包含段及其目的节点ID)给SIP进程.
//参数sip_conn是在STCP进程和SIP进程之间连接的TCP描述符.
//如果sendseg_arg_t发送成功,就返回0,否则返回-1.
int sip_sendseg(int sip_conn, int dest_nodeID, seg_t* segPtr)
{
    segPtr->header.checksum=checksum(segPtr);
    while(pthread_mutex_trylock(&stcp_send_mutex))usleep(10);
    if(2!=send(sip_conn, flag_head,2, 0)){
        pthread_mutex_unlock(&stcp_send_mutex);
        return -1;
    }
    if(sizeof(int)!=send(sip_conn, &dest_nodeID,sizeof(int), 0)){
        pthread_mutex_unlock(&stcp_send_mutex);
        return -1;
    }
    if((segPtr->header.length+sizeof(stcp_hdr_t))!=send(sip_conn, segPtr,segPtr->header.length+sizeof(stcp_hdr_t), 0)){
        pthread_mutex_unlock(&stcp_send_mutex);
        return -1;
    }
    if(2!=send(sip_conn, flag_end,2, 0)){
        pthread_mutex_unlock(&stcp_send_mutex);
        return -1;
    }

    pthread_mutex_unlock(&stcp_send_mutex);
#ifdef DEBUG_SEG
    printf("[STCP][Send]nodeID:%d    PORT:%d to %d,Type:%d Seq:%d Ack:%d\n", dest_nodeID,segPtr->header.src_port, segPtr->header.dest_port,segPtr->header.type, segPtr->header.seq_num, segPtr->header.ack_num);
#endif
    return 0;
}

//STCP进程使用这个函数来接收来自SIP进程的包含段及其源节点ID的sendseg_arg_t结构.
//参数sip_conn是STCP进程和SIP进程之间连接的TCP描述符.
//当接收到段时, 使用seglost()来判断该段是否应被丢弃并检查校验和.
//如果成功接收到sendseg_arg_t就返回1, 否则返回-1.
int sip_recvseg(int sip_conn, int* src_nodeID, seg_t* segPtr)
{
    char check[3];

    check[2]=0;
    while(strcmp(check,"!&"))
    {
        check[0]=check[1];
        recv(sip_conn, check+1, 1, 0);
    }
    recv(sip_conn,src_nodeID,sizeof(int),MSG_WAITALL);
    recv(sip_conn,&segPtr->header,sizeof(stcp_hdr_t),MSG_WAITALL);
    printf("\n");
    recv(sip_conn,segPtr->data,segPtr->header.length,MSG_WAITALL);
    recv(sip_conn, check, 2, MSG_WAITALL);

    /*{
        char *p=segPtr->data;
        int c=segPtr->header.length;
        printf("[DATA]:\n");
        while(c--){
            printf("%c",*p++);
        }
    }*/

    if(strcmp(check,"!#")){
	    printf("[END FLAG wrong!!!]%02x	%02x\n",check[0],check[1]);
	    return -1;
    }
    if(seglost(segPtr)){
    #ifdef DEBUG_SEG
        printf("seg lost!\n");
    #endif
        return -1;
    }

    if( checkchecksum(segPtr)==0 ){
    #ifdef DEBUG_SEG
    	printf("[STCP][Receive] %d to %d,Type:%d Seq:%d Ack:%d\n", segPtr->header.src_port, segPtr->header.dest_port, segPtr->header.type,segPtr->header.seq_num, segPtr->header.ack_num);
    #endif
        return 1;
    }
    else{
    #ifdef DEBUG_SEG
        printf("Checksum wrong!\n");
    #endif
    	return -1;
    }
}

//SIP进程使用这个函数接收来自STCP进程的包含段及其目的节点ID的sendseg_arg_t结构.
//参数stcp_conn是在STCP进程和SIP进程之间连接的TCP描述符.
//如果成功接收到sendseg_arg_t就返回1, 否则返回-1.
int getsegToSend(int stcp_conn, int* dest_nodeID, seg_t* segPtr)
{
    char check[3];

    check[2]=0;
    while(strcmp(check,"!&"))
    {
        check[0]=check[1];
        recv(stcp_conn, check+1, 1, 0);
    }
    recv(stcp_conn,dest_nodeID,sizeof(int),MSG_WAITALL);
    recv(stcp_conn,&segPtr->header,sizeof(stcp_hdr_t),MSG_WAITALL);
    recv(stcp_conn,segPtr->data,segPtr->header.length,MSG_WAITALL);
    recv(stcp_conn, check, 2, MSG_WAITALL);

    if(strcmp(check,"!#")){
	    printf("[END FLAG wrong!!!]%02x	%02x\n",check[0],check[1]);
	    return -1;
    }
    else {
#ifdef DEBUG_SEG
        printf("[SIP][Recvfrom][STCP]\n");
#endif
        return 1;
    }
}

//SIP进程使用这个函数发送包含段及其源节点ID的sendseg_arg_t结构给STCP进程.
//参数stcp_conn是STCP进程和SIP进程之间连接的TCP描述符.
//如果sendseg_arg_t被成功发送就返回1, 否则返回-1.
int forwardsegToSTCP(int stcp_conn, int src_nodeID, seg_t* segPtr)
{
    segPtr->header.checksum=checksum(segPtr);
    while(pthread_mutex_trylock(&sip_send_mutex))usleep(10);
    if(2!=send(stcp_conn, flag_head,2, 0)){
        pthread_mutex_unlock(&sip_send_mutex);
        return -1;
    }
    if(sizeof(int)!=send(stcp_conn, &src_nodeID,sizeof(int), 0)){
        pthread_mutex_unlock(&sip_send_mutex);
        return -1;
    }
    if((segPtr->header.length+sizeof(stcp_hdr_t))!=send(stcp_conn, segPtr,segPtr->header.length+sizeof(stcp_hdr_t), 0)){
        pthread_mutex_unlock(&sip_send_mutex);
        return -1;
    }
    if(2!=send(stcp_conn, flag_end,2, 0)){
        pthread_mutex_unlock(&sip_send_mutex);
        return -1;
    }
    pthread_mutex_unlock(&sip_send_mutex);
#ifdef DEBUG_SEG
    printf("[SIP][Sendto][STCP]nodeID:%d    PORT:%d to %d,Type:%d Seq:%d Ack:%d\n", src_nodeID,segPtr->header.src_port, segPtr->header.dest_port,segPtr->header.type, segPtr->header.seq_num, segPtr->header.ack_num);
#endif

    return 1;
}

// 一个段有PKT_LOST_RATE/2的可能性丢失, 或PKT_LOST_RATE/2的可能性有着错误的校验和.
// 如果数据包丢失了, 就返回1, 否则返回0.
// 即使段没有丢失, 它也有PKT_LOST_RATE/2的可能性有着错误的校验和.
// 我们在段中反转一个随机比特来创建错误的校验和.
int seglost(seg_t* segPtr) {
	int random = rand()%100;
	if(random<PKT_LOSS_RATE*100) {
		//50%可能性丢失段
		if(rand()%2==0) {
            return 1;
		}
		//50%可能性是错误的校验和
		else {
			//获取数据长度
			int len = sizeof(stcp_hdr_t)+segPtr->header.length;
			//获取要反转的随机位
			int errorbit = rand()%(len*8);
			//反转该比特
			char* temp = (char*)segPtr;
			temp = temp + errorbit/8;
			*temp = *temp^(1<<(errorbit%8));
			return 0;
		}
	}
	return 0;
}

//这个函数计算指定段的校验和.
//校验和计算覆盖段首部和段数据. 你应该首先将段首部中的校验和字段清零,
//如果数据长度为奇数, 添加一个全零的字节来计算校验和.
//校验和计算使用1的补码.
unsigned short checksum(seg_t* segment){
    int i,n=segment->header.length+sizeof(stcp_hdr_t);
    unsigned short *buf=(unsigned short *)segment;
    unsigned long sum = 0;

    segment->header.checksum=0;
    for(i=0; i<n/2; i++){
        sum += *(buf++);
    }

    if(n%2){
        sum += (*buf)&0xff00;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);

    return (unsigned short)~sum;
}

//这个函数检查段中的校验和, 正确时返回1, 错误时返回-1.
int checkchecksum(seg_t* segment){
    unsigned short sum_buf=segment->header.checksum;
    if(sum_buf==checksum(segment))return 0;
    else return 1;
}
