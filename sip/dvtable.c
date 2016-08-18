
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/constants.h"
#include "../topology/topology.h"
#include "dvtable.h"

//这个函数动态创建距离矢量表.
//距离矢量表包含n+1个条目, 其中n是这个节点的邻居数,剩下1个是这个节点本身.
//距离矢量表中的每个条目是一个dv_t结构,它包含一个源节点ID和一个有N个dv_entry_t结构的数组, 其中N是重叠网络中节点总数.
//每个dv_entry_t包含一个目的节点地址和从该源节点到该目的节点的链路代价.
//距离矢量表也在这个函数中初始化.从这个节点到其邻居的链路代价使用提取自topology.dat文件中的直接链路代价初始化.
//其他链路代价被初始化为INFINITE_COST.
//该函数返回动态创建的距离矢量表.
dv_t* dvtable_create()
{
    dv_t *out;
    int nbrNum=topology_getNbrNum();
    int nodeNum=topology_getNodeNum();
    int myID=topology_getMyNodeID();
    int *nbrIDs=topology_getNbrArray();
    int *nodeIDs=topology_getNodeArray();
    int i,j;

    out=malloc((nbrNum+1)*sizeof(dv_t));
    for(i=0;i<nbrNum;++i){
        out[i].nodeID=nbrIDs[i];
        out[i].dvEntry=malloc(nodeNum*sizeof(dv_entry_t));
        for(j=0;j<nodeNum;++j){
            out[i].dvEntry[j].nodeID=nodeIDs[j];
            out[i].dvEntry[j].cost=INFINITE_COST;//topology_getCost(out[i].nodeID,out[i].dvEntry[j].nodeID);
        }
    }
    out[i].nodeID=myID;
    out[i].dvEntry=malloc(nodeNum*sizeof(dv_entry_t));
    for(j=0;j<nodeNum;++j){
        out[i].dvEntry[j].nodeID=nodeIDs[j];
        out[i].dvEntry[j].cost=topology_getCost(out[i].nodeID,out[i].dvEntry[j].nodeID);
    }

    free(nbrIDs);
    free(nodeIDs);

    return out;
}

//这个函数删除距离矢量表.
//它释放所有为距离矢量表动态分配的内存.
void dvtable_destroy(dv_t* dvtable)
{
    int Num=1+topology_getNbrNum();
    int i;

    for(i=0;i<Num;++i){
        free(dvtable[i].dvEntry);
    }
    free(dvtable);

    return;
}

//这个函数设置距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,并且链路代价也被成功设置了,就返回1,否则返回-1.
int dvtable_setcost(dv_t* dvtable,int fromNodeID,int toNodeID, unsigned int cost)
{
    int Num=1+topology_getNbrNum();
    int nodeNum=topology_getNodeNum();
    int i,j;

    printf("[dvtable_setcost]from %d to %d,cost=%d\t",fromNodeID,toNodeID,cost);

    for(i=0;i<Num;++i){
        if(dvtable[i].nodeID==fromNodeID){
            for(j=0;j<nodeNum;++j){
                if(dvtable[i].dvEntry[j].nodeID==toNodeID){
                    if(cost==DOUBLE)dvtable[i].dvEntry[j].cost=2*dvtable[i].dvEntry[j].cost;
                    else if(cost==JIANER){
                        if(dvtable[i].dvEntry[j].cost>2)dvtable[i].dvEntry[j].cost=dvtable[i].dvEntry[j].cost-2;
                    }
                    else dvtable[i].dvEntry[j].cost=cost;
                    if(dvtable[i].dvEntry[j].cost>INFINITE_COST)dvtable[i].dvEntry[j].cost=INFINITE_COST;
                    printf("[Succeed]cost=%d\n",dvtable[i].dvEntry[j].cost);
                    return 1;
                }
            }
        }
    }
    printf("[Failed]\n");
    return -1;
}

//这个函数返回距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,就返回链路代价,否则返回INFINITE_COST.
unsigned int dvtable_getcost(dv_t* dvtable, int fromNodeID, int toNodeID)
{
    int Num=1+topology_getNbrNum();
    int nodeNum=topology_getNodeNum();
    int i,j;

    if(fromNodeID==toNodeID)return 0;

    for(i=0;i<Num;++i){
        if(dvtable[i].nodeID==fromNodeID){
            for(j=0;j<nodeNum;++j){
                if(dvtable[i].dvEntry[j].nodeID==toNodeID){
                    return dvtable[i].dvEntry[j].cost;
                }
            }
        }
    }

    return INFINITE_COST;
}

//这个函数打印距离矢量表的内容.
void dvtable_print(dv_t* dvtable)
{
    int Num=1+topology_getNbrNum();
    int nodeNum=topology_getNodeNum();
    int *nodeIDs=topology_getNodeArray();
    int i,j;

    printf("====================\n");
    printf("Distance Vector Table\n");
    printf("====================\n");

    for(i=0;i<nodeNum;++i){
        printf("\t%d",nodeIDs[i]);
    }
    printf("\n");
    for(i=0;i<Num;++i){
        printf("%d\t",dvtable[i].nodeID);
        for(j=0;j<nodeNum;++j){
            printf("%d\t",dvtable[i].dvEntry[j].cost);
        }
	printf("\n");
    }
    printf("====================\n");

    free(nodeIDs);

    return;
}
