//文件名: sip/sip.c
//
//描述: 这个文件实现SIP进程
//
//创建日期: 2015年

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <assert.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "../common/seg.h"
#include "../topology/topology.h"
#include "sip.h"
#include "nbrcosttable.h"
#include "dvtable.h"
#include "routingtable.h"

//SIP层等待这段时间让SIP路由协议建立路由路径.
#define SIP_WAITTIME 20

/**************************************************************/
//声明全局变量
/**************************************************************/
int son_conn; 			//到重叠网络的连接
int stcp_conn;			//到STCP的连接
int sock_listo_stcp;
int *NodeIDs;
int quality[MAX_NODE_NUM]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
nbr_cost_entry_t* nct;			//邻居代价表
dv_t* dv;				//距离矢量表
pthread_mutex_t* dv_mutex;		//距离矢量表互斥量
routingtable_t* routingtable;		//路由表
pthread_mutex_t* routingtable_mutex;	//路由表互斥量
pthread_t updater_thread;
struct timespec tim;

/**************************************************************/
//实现SIP的函数
/**************************************************************/

//SIP进程使用这个函数连接到本地SON进程的端口SON_PORT.
//成功时返回连接描述符, 否则返回-1.
int connectToSON() {
	//你需要编写这里的代码.
	char myname[128];
	struct hostent *myhost;
	struct sockaddr_in server_addr;

    gethostname(myname,sizeof(myname));
    myhost=gethostbyname(myname);
    if(NULL==myhost)return -1;

	bzero(&server_addr,sizeof(server_addr));
	server_addr.sin_family=AF_INET;
	server_addr.sin_port=htons(SON_PORT);
	server_addr.sin_addr=*((struct in_addr*)myhost->h_addr_list[0]);

    son_conn=socket(AF_INET,SOCK_STREAM,0);
	if(son_conn==-1)
	{
		printf("Socket Error:%s\a\n",strerror(errno));
		return -1;
	}

	if(connect(son_conn,(struct sockaddr *)&server_addr,sizeof(struct sockaddr))==-1)
	{
        //unsigned char *p=(unsigned char *)&server_addr.sin_addr;
        //printf("Connect to son,IP %u.%u.%u.%u\n",p[0],p[1],p[2],p[3]);
		printf("Connect Error:%s\a\n",strerror(errno));
		return -1;
	}
	unsigned char *p=(unsigned char *)&server_addr.sin_addr;
    printf("Connect to son,IP %u.%u.%u.%u   PORT:%d\n",p[0],p[1],p[2],p[3],SON_PORT);
	printf("TCP connected!\n");

	return son_conn;
}

//这个线程每隔ROUTEUPDATE_INTERVAL时间发送路由更新报文.路由更新报文包含这个节点
//的距离矢量.广播是通过设置SIP报文头中的dest_nodeID为BROADCAST_NODEID,并通过son_sendpkt()发送报文来完成的.
void* routeupdate_daemon(void* arg) {
	//你需要编写这里的代码.
    sip_pkt_t tmp;
    dv_t *p=dv;
    int myID=topology_getMyNodeID();
    int nodeNum=topology_getNodeNum();

	tmp.header.dest_nodeID=BROADCAST_NODEID;
	tmp.header.length=sizeof(pkt_routeupdate_t);
	tmp.header.src_nodeID=myID;
	tmp.header.type=ROUTE_UPDATE;

	while(1){
        printf("[routeupdate_daemon]dv_mutex[trylock]\t");
        printf("return:%d\t",pthread_mutex_lock(dv_mutex));/////////////////
        printf("[locked]\t");
        while(p->nodeID!=myID)++p;
        memcpy(tmp.data,&nodeNum,sizeof(int));
        memcpy(tmp.data+sizeof(int),p->dvEntry,nodeNum*sizeof(dv_entry_t));
        pthread_mutex_unlock(dv_mutex);/////////////////////////
        printf("[unlock]\n");
        son_sendpkt(BROADCAST_NODEID, &tmp, son_conn);
        sleep(ROUTEUPDATE_INTERVAL);
	}
    return (void *)0;
}

//这个线程负责更新距离矢量表和路由表，每次收到路由更新报文后被启动，输入参数为报文指针
void* updater(void* arg){
    int srcID;
    pkt_routeupdate_t data;
    dv_t* dv_p=dv;
    int num=1+topology_getNbrNum();
    const int nodeNum=topology_getNodeNum();
    const int myID=topology_getMyNodeID();
    int i,j,cost,nextcost,nextID;
    int flag_refresh;
    sip_pkt_t *tmp=arg;

    memcpy(&srcID,(char *)arg,sizeof(int));
    memcpy(&data,((char *)arg)+sizeof(sip_hdr_t),sizeof(pkt_routeupdate_t));

     if(tmp->header.type==NBR_UPDATE){
        printf("Receive nbr_update refresh.....\n");
        nbrcosttable_setcost(nct,data.entry[0].nodeID,data.entry[0].cost);
        nbrcosttable_print(nct);
    }
    else if(tmp->header.type==ROUTE_UPDATE){
        printf("Receive rout_update refresh.....\n");

        printf("[updater]dv_mutex[trylock]\t");
        printf("return:%d\t",pthread_mutex_lock(dv_mutex));
        printf("[locked]\t");
        while(data.entryNum--){
            dvtable_setcost(dv,srcID,data.entry[data.entryNum].nodeID,data.entry[data.entryNum].cost);
        }
        pthread_mutex_unlock(dv_mutex);
        printf("[unlock]\n");
    }
    //else return (void *)1;
    //memcpy(dv_p->dvEntry,&data.entry,data.entryNum*sizeof(routeupdate_entry_t));
    //pthread_mutex_unlock(dv_mutex);
    num=1+topology_getNbrNum();
    for(i=0;i<num;++i){
        if(dv[i].nodeID==myID){
            dv_p=dv+i;
            break;
        }
    }
    if(i==num){
        printf("Can't find dvtable of local machine!\n");
        return (void *)-1;
    }

    printf("[updater]dv_mutex&routingtable_mutex[trylock]\t");
    printf("return:%d\t",pthread_mutex_lock(dv_mutex));//////////////////////
    //printf("return:%d\t",pthread_mutex_lock(routingtable_mutex));
    printf("[locked]\t");
    for(i=0;i<nodeNum;++i){
        flag_refresh=0;
        nextID=routingtable_getnextnode(routingtable, dv_p->dvEntry[i].nodeID);
        if(nextID==-1){
            dv_p->dvEntry[i].cost=INFINITE_COST;
        }
        else{
            nextcost=dvtable_getcost(dv, nextID, dv_p->dvEntry[i].nodeID);
            cost=nbrcosttable_getcost(nct, nextID);
            dv_p->dvEntry[i].cost=cost+nextcost;
        }
        for(j=0;j<num;++j){
            cost=nbrcosttable_getcost(nct, dv[j].nodeID);
            nextcost=dvtable_getcost(dv, dv[j].nodeID, dv_p->dvEntry[i].nodeID);
            if((cost+nextcost)<dv_p->dvEntry[i].cost){
                dv_p->dvEntry[i].cost=cost+nextcost;
                routingtable_setnextnode(routingtable, dv_p->dvEntry[i].nodeID, dv[j].nodeID);
                flag_refresh=1;
            }
        }
        //if(0==flag_refresh)routingtable_setnextnode(routingtable, dv_p->dvEntry[i].nodeID, dv_p->dvEntry[i].nodeID);
    }
    pthread_mutex_unlock(dv_mutex);
    //pthread_mutex_unlock(routingtable_mutex);////////////////////////
    printf("[unlock]\n");

    dvtable_print(dv);
    routingtable_print(routingtable);

    return (void*)0;
}

//这个线程处理来自SON进程的进入报文. 它通过调用son_recvpkt()接收来自SON进程的报文.
//如果报文是SIP报文,并且目的节点就是本节点,就转发报文给STCP进程. 如果目的节点不是本节点,
//就根据路由表转发报文给下一跳.如果报文是路由更新报文,就更新距离矢量表和路由表.
void* pkthandler(void* arg) {
	//你需要编写这里的代码.
	sip_pkt_t tmp;
	seg_t *out;
	sip_pkt_t update;
	int myID=topology_getMyNodeID();
	int nextID;

	while(1){
        if(-1==son_recvpkt(&tmp,son_conn))continue;
        if(tmp.header.type==SIP){
            if(tmp.header.dest_nodeID==myID){
                out=(seg_t *)&tmp.data;
                if(-1==forwardsegToSTCP(stcp_conn,tmp.header.src_nodeID,out))printf("[Pkthandler]send to stcp failed!\n");
            }
            else{
                nextID=routingtable_getnextnode(routingtable, tmp.header.dest_nodeID);
                if(nextID==-1)continue;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                son_sendpkt(nextID, &tmp, son_conn);
                /*while(pthread_mutex_trylock(dv_mutex))usleep(10);
                cost=dvtable_getcost(dv, myID,nextID);
                if(1==re){
                    if(cost>2)cost=cost-2;
                }
                else{
                    printf("Send failed,set double cost!!!");
                    cost=cost*2;
                }
                if(cost>INFINITE_COST)cost=INFINITE_COST;
                if(1==dvtable_setcost(dv,myID,nextID, cost))printf("\t[Success]\n");
                else printf("\t[Failed]\n");
                pthread_mutex_unlock(dv_mutex);*/
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            }
        }
        if(tmp.header.type==ROUTE_UPDATE||tmp.header.type==NBR_UPDATE){
            memcpy(&update,&tmp,sizeof(sip_pkt_t));
            //pthread_join(updater_thread,NULL);
            pthread_create(&updater_thread,NULL,updater,(void *)&update);
        }
	}
    return (void *)0;
}

//这个函数终止SIP进程, 当SIP进程收到信号SIGINT时会调用这个函数.
//它关闭所有连接, 释放所有动态分配的内存.
void sip_stop() {
	//你需要编写这里的代码.
	close(sock_listo_stcp);
	close(stcp_conn);
	close(son_conn);
	free(nct);
    free(dv);
    free(routingtable);
    free(NodeIDs);
    pthread_mutex_destroy(dv_mutex);
    pthread_mutex_destroy(routingtable_mutex);
    free(dv_mutex);
    free(routingtable_mutex);

    return;
}

//这个函数打开端口SIP_PORT并等待来自本地STCP进程的TCP连接.
//在连接建立后, 这个函数从STCP进程处持续接收包含段及其目的节点ID的sendseg_arg_t.
//接收的段被封装进数据报(一个段在一个数据报中), 然后使用son_sendpkt发送该报文到下一跳. 下一跳节点ID提取自路由表.
//当本地STCP进程断开连接时, 这个函数等待下一个STCP进程的连接.
void waitSTCP() {
	//你需要编写这里的代码.
	struct sockaddr_in ipaddr;
	int i;
	int myID=topology_getMyNodeID();
	int nextNodeID;
	char check[3];
	stcp_hdr_t *head;
	sip_pkt_t buf;
	//char *seg_dat=buf.data+sizeof(stcp_hdr_t);

    printf("Start 'waitSTCP'\n");
	sock_listo_stcp=socket(AF_INET, SOCK_STREAM, 0);

	i=sizeof(sip_pkt_t)*100;

	setsockopt(sock_listo_stcp, SOL_SOCKET, SO_RCVBUF, (const char *)&i, sizeof(i));
	i=1;
	setsockopt(sock_listo_stcp, SOL_SOCKET, SO_REUSEADDR, (const char *)&i, sizeof(i));
	memset(&ipaddr, 0, sizeof(ipaddr));
	ipaddr.sin_family=AF_INET;
	ipaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	ipaddr.sin_port=htons(SIP_PORT);

	bind(sock_listo_stcp, (struct sockaddr*) &ipaddr, sizeof(ipaddr));
    printf("TCP listening to stcp.....\n");
	listen(sock_listo_stcp, 10);
	stcp_conn=accept(sock_listo_stcp, NULL, NULL);
	printf("STCP CONNECTED!\n");

    check[2]=0;
    buf.header.src_nodeID=myID;
    buf.header.type=SIP;
	while(1){
        while(strcmp(check,"!&"))
        {
            check[0]=check[1];
            recv(stcp_conn, check+1, 1, MSG_WAITALL);
        }
        head=(stcp_hdr_t *)&buf.data;
        if(-1==getsegToSend(stcp_conn,&buf.header.dest_nodeID,(seg_t *)&buf.data)){
            printf("[Waitstcp]get seg to send failed!\n");
            continue;
        }
        buf.header.length=head->length+sizeof(stcp_hdr_t);

        nextNodeID=routingtable_getnextnode(routingtable, buf.header.dest_nodeID);
        //printf("nextNodeID=%d\n",nextNodeID);
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        son_sendpkt(nextNodeID, &buf, son_conn);
        /*while(pthread_mutex_trylock(dv_mutex))usleep(10);
        if(1==re){
            if(cost>2)cost=cost-2;
        }
        else{
            printf("Send failed,set double cost!!!");
            cost=cost*2;
        }
        if(cost>INFINITE_COST)cost=INFINITE_COST;
        //if(1==dvtable_setcost(dv,myID,nextNodeID, cost))printf("\t[Success]\n");
        //else printf("\t[Failed]\n");
        pthread_mutex_unlock(dv_mutex);*/
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    }

	return;
}

int main(int argc, char *argv[]) {
	printf("SIP layer is starting, pls wait...\n");

	topology_init();//解析dat文件

	//初始化全局变量
	tim.tv_sec=0;
	tim.tv_nsec=10000;
	NodeIDs=topology_getNodeArray();
	nct = nbrcosttable_create();
	dv = dvtable_create();
	dv_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(dv_mutex,NULL);
	routingtable = routingtable_create();
	routingtable_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(routingtable_mutex,NULL);
	son_conn = -1;
	stcp_conn = -1;

	nbrcosttable_print(nct);
	dvtable_print(dv);
	routingtable_print(routingtable);

	//注册用于终止进程的信号句柄
	signal(SIGINT, sip_stop);

	//连接到本地SON进程
	son_conn = connectToSON();
	if(son_conn<0) {
		printf("can't connect to SON process\n");
		exit(1);
	}

	//启动线程处理来自SON进程的进入报文
	pthread_t pkt_handler_thread;
	pthread_create(&pkt_handler_thread,NULL,pkthandler,(void*)0);

	//启动路由更新线程
	pthread_t routeupdate_thread;
	pthread_create(&routeupdate_thread,NULL,routeupdate_daemon,(void*)0);

	printf("SIP layer is started...\n");
	printf("waiting for routes to be established\n");
	sleep(SIP_WAITTIME);
	routingtable_print(routingtable);

	//等待来自STCP进程的连接
	printf("waiting for connection from STCP process\n");
	waitSTCP();

    return 0;
}


