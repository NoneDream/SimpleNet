
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "nbrcosttable.h"
#include "../common/constants.h"
#include "../topology/topology.h"

//这个函数动态创建邻居代价表并使用邻居节点ID和直接链路代价初始化该表.
//邻居的节点ID和直接链路代价提取自文件topology.dat.
nbr_cost_entry_t* nbrcosttable_create()
{
    nbr_cost_entry_t *out;
    int nbrNum=topology_getNbrNum();
    int myID=topology_getMyNodeID();
    int *nbrIDs=topology_getNbrArray();
    int i;

    out=malloc(nbrNum*sizeof(nbr_cost_entry_t));
    for(i=0;i<nbrNum;++i){
        out[i].nodeID=nbrIDs[i];
        out[i].cost=topology_getCost(myID,out[i].nodeID);
    }

    free(nbrIDs);

    return out;
}

//这个函数删除邻居代价表.
//它释放所有用于邻居代价表的动态分配内存.
void nbrcosttable_destroy(nbr_cost_entry_t* nct)
{
    free(nct);

    return;
}

//这个函数用于获取邻居的直接链路代价.
//如果邻居节点在表中发现,就返回直接链路代价.否则返回INFINITE_COST.
unsigned int nbrcosttable_getcost(nbr_cost_entry_t* nct, int nodeID)
{
    int nbrNum=topology_getNbrNum();
    int myID=topology_getMyNodeID();
    int i;

    if(nodeID==myID)return 0;

    for(i=0;i<nbrNum;++i){
        if(nct[i].nodeID==nodeID)return nct[i].cost;
    }

    return INFINITE_COST;
}

//这个函数设置邻居代价成功返回1，否则返回0.
int nbrcosttable_setcost(nbr_cost_entry_t* nct, int nodeID,int cost)
{
    int nbrNum=topology_getNbrNum();
    int i;

    for(i=0;i<nbrNum;++i){
        if(nct[i].nodeID==nodeID){
            if(cost==DOUBLE)nct[i].cost=2*nct[i].cost;
            else if(cost==JIANER){
                if(nct[i].cost>2)nct[i].cost=nct[i].cost-2;
            }
            else nct[i].cost=cost;
            if(nct[i].cost>INFINITE_COST)nct[i].cost=INFINITE_COST;
            return 1;
        }
    }

    return 0;
}

//这个函数打印邻居代价表的内容.
void nbrcosttable_print(nbr_cost_entry_t* nct)
{
    int nbrNum=topology_getNbrNum();
    int i;

    printf("====================\n");
    printf("Distance Cost Table\n");
    printf("====================\n");

    for(i=0;i<nbrNum;++i){
        printf("nodeID:%d   cost:%d\n",nct[i].nodeID,nct[i].cost);
    }
    printf("====================\n");

    return;
}
