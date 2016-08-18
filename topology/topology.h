//文件名: topology/topology.h
//
//描述: 这个文件声明一些用于解析拓扑文件的辅助函数
//
//创建日期: 2015年

#ifndef TOPOLOGY_H
#define TOPOLOGY_H
#include <netdb.h>
#include <arpa/inet.h>
#include "../common/constants.h"

typedef struct{
    int ID0;
    int ID1;
    in_addr_t IP0;
    in_addr_t IP1;
    int cost;
}cost_t;

typedef struct{
    int num;
    cost_t info[MAX_NODE_NUM];
}dat_t;

//这个函数解析dat文件，将解析到的数据保存在全局变量中
void topology_init(void);
//这个函数将数据复制一份到输入指针指向的位置
void filetranslate(dat_t *p);

//这个函数返回指定主机的节点ID.
//节点ID是节点IP地址最后8位表示的整数.
//例如, 一个节点的IP地址为202.119.32.12, 它的节点ID就是12.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromname(char* hostname);

//这个函数返回指定的IP地址的节点ID.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromip(struct in_addr* addr);

//这个函数返回本机的节点ID
//如果不能获取本机的节点ID, 返回-1.
int topology_getMyNodeID();
int getMyNodeID();

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回邻居数.
int topology_getNbrNum();
int getNbrNum();

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回重叠网络中的总节点数.
int topology_getNodeNum();
int getNodeNum();

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含重叠网络中所有节点的ID.
int* topology_getNodeArray();
int* getNodeArray();

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含所有邻居的节点ID.
int* topology_getNbrArray();
int* getNbrArray();

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回指定两个节点之间的直接链路代价.
//如果指定两个节点之间没有直接链路, 返回INFINITE_COST.
unsigned int topology_getCost(int fromNodeID, int toNodeID);

//这个函数释放所有动态分配的内存。
void topology_free();
#endif
