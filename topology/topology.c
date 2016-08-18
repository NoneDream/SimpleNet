//文件名: topology/topology.c
//
//描述: 这个文件实现一些用于解析拓扑文件的辅助函数
//
//创建日期: 2015年

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <string.h>
#include <errno.h>

#include "topology.h"

dat_t data_buff;
int myNodeID,NbrNum,NodeNum;
int *NodeArray,*NbrArray;

void topology_free(){
    free(NodeArray);
    free(NbrArray);
    return;
}

//这个函数解析dat文件，将解析到的数据保存在全局变量中
void topology_init(){
    char name[2][128];
    FILE *fp;
    int c;
    struct hostent *host;

    fp=NULL;
    fp=fopen("../topology/topology.dat","r");
    if(NULL==fp){
        printf("Can't open '.dat' file!\n");
        printf("File open error:%s\a\n",strerror(errno));
        data_buff.num=-1;
        exit(-1);
    }

    data_buff.num=0;
    while(3==fscanf(fp,"%s %s %d\n",name[0],name[1],&c)){
        host=gethostbyname(name[0]);
        if(host==NULL)continue;
        memcpy(&data_buff.info[data_buff.num].IP0,host->h_addr_list[0],sizeof(data_buff.info[data_buff.num].IP0));
        data_buff.info[data_buff.num].ID0=topology_getNodeIDfromip((struct in_addr*)host->h_addr_list[0]);
        host=gethostbyname(name[1]);
        memcpy(&data_buff.info[data_buff.num].IP1,host->h_addr_list[0],sizeof(data_buff.info[data_buff.num].IP1));
        data_buff.info[data_buff.num].ID1=topology_getNodeIDfromip((struct in_addr*)host->h_addr_list[0]);
        data_buff.info[data_buff.num].cost=c;
        ++data_buff.num;
    }
    fclose(fp);

    myNodeID=getMyNodeID();
    NbrNum=getNbrNum();
    NodeNum=getNodeNum();
    NodeArray=getNodeArray();
    NbrArray=getNbrArray();

    return;
}

//这个函数将数据复制一份到输入指针指向的位置
void filetranslate(dat_t *p){
    memcpy(p,&data_buff,sizeof(dat_t));
}

//这个函数返回指定主机的节点ID.
//节点ID是节点IP地址最后8位表示的整数.
//例如, 一个节点的IP地址为202.119.32.12, 它的节点ID就是12.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromname(char* hostname)
{
    struct hostent *host;
    int tmp;

    host=gethostbyname(hostname);
    if(NULL==host)return -1;
    tmp=topology_getNodeIDfromip((struct in_addr*)host->h_addr_list[0]);
    //free(host);

    return tmp;
}

//这个函数返回指定的IP地址的节点ID.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromip(struct in_addr* addr)
{
    unsigned char *tmp=(unsigned char *)addr;
    return (int)tmp[3];
}

//这个函数返回本机的节点ID
//如果不能获取本机的节点ID, 返回-1.
int topology_getMyNodeID(){
    return myNodeID;
}
int getMyNodeID()
{
    char myname[128];
    struct hostent *myhost;
    int tmp;

    gethostname(myname,sizeof(myname));
    //uname(myname);
    myhost=gethostbyname(myname);
    if(NULL==myhost)return -1;
    tmp=topology_getNodeIDfromip((struct in_addr*)myhost->h_addr_list[0]);
    //free(host);

    return tmp;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回邻居数.
int topology_getNbrNum(){
    return NbrNum;
}
int getNbrNum()
{
    dat_t data;
    int myID=topology_getMyNodeID();
    int count=0;

    filetranslate(&data);

    while(data.num--){
        if(myID==data.info[data.num].ID0||myID==data.info[data.num].ID1){
            ++count;
        }
    }

    return count;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回重叠网络中的总节点数.
int topology_getNodeNum(){
    return NodeNum;
}
int getNodeNum()
{
    dat_t data;
    int count=0;
    int new_flag,j;
    int ID[MAX_NODE_NUM];

    filetranslate(&data);

    while(data.num--){
        new_flag=1,j=0;
        while(j<=count){
            if(ID[j]==data.info[data.num].ID0){
                new_flag=0;
                break;
            }
            ++j;
        }
        if(new_flag){
            ID[count]=data.info[data.num].ID0;
            ++count;
        }

        new_flag=1,j=0;
        while(j<=count){
            if(ID[j]==data.info[data.num].ID1){
                new_flag=0;
                break;
            }
            ++j;
        }
        if(new_flag){
            ID[count]=data.info[data.num].ID1;
            ++count;
        }
    }

    return count;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含重叠网络中所有节点的ID.
int* topology_getNodeArray(){
    int *out;
    int len=NodeNum*sizeof(int);

    out=malloc(len);
    memcpy(out,NodeArray,len);

    return out;
}
int* getNodeArray()
{
    dat_t data;
    int *out;
    int count=0;
    int new_flag,j;
    int ID[MAX_NODE_NUM];

    filetranslate(&data);

    while(data.num--){
    ////////////////////////////////////////////////////////////////////
        new_flag=1,j=0;
        while(j<count){
            if(ID[j]==data.info[data.num].ID0){
                new_flag=0;
                break;
            }
            ++j;
        }
        if(new_flag){
            ID[count]=data.info[data.num].ID0;
            ++count;
        }
//////////////////////////////////////////////////////////////////////
        new_flag=1,j=0;
        while(j<count){
            if(ID[j]==data.info[data.num].ID1){
                new_flag=0;
                break;
            }
            ++j;
        }
        if(new_flag){
            ID[count]=data.info[data.num].ID1;
            ++count;
        }
/////////////////////////////////////////////////////////////////////////
    }

    out=malloc(count*sizeof(int));
    memcpy(out,ID,count*sizeof(int));

    return out;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含所有邻居的节点ID.
int* topology_getNbrArray(){
    int *out;
    int len=NbrNum*sizeof(int);

    out=malloc(len);
    memcpy(out,NbrArray,len);

    return out;
}
int* getNbrArray()
{
    dat_t data;
    int *out;
    int ID[MAX_NODE_NUM];
    int myID=topology_getMyNodeID();
    int count=0;

    filetranslate(&data);

    while(data.num--){
        if(myID==data.info[data.num].ID0){
            ID[count]=data.info[data.num].ID1;
            ++count;
        }

        else if(myID==data.info[data.num].ID1){
            ID[count]=data.info[data.num].ID0;
            ++count;
        }
    }

    out=malloc(count*sizeof(int));
    memcpy(out,ID,count*sizeof(int));

    return out;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回指定两个节点之间的直接链路代价.
//如果指定两个节点之间没有直接链路, 返回INFINITE_COST.
unsigned int topology_getCost(int fromNodeID, int toNodeID)
{
    dat_t data;

    if(fromNodeID==toNodeID)return 0;

    filetranslate(&data);

    while(data.num--){
        if(fromNodeID==data.info[data.num].ID0&&toNodeID==data.info[data.num].ID1){
            return data.info[data.num].cost;
        }

        else if(fromNodeID==data.info[data.num].ID1&&toNodeID==data.info[data.num].ID0){
            return data.info[data.num].cost;
        }
    }

    return INFINITE_COST;
}
