//文件名: son/neighbortable.c
//
//描述: 这个文件实现用于邻居表的API
//
//创建日期: 2015年

#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <malloc.h>
#include <unistd.h>
#include "neighbortable.h"
#include "../topology/topology.h"

//这个函数首先动态创建一个邻居表. 然后解析文件topology/topology.dat, 填充所有条目中的nodeID和nodeIP字段, 将conn字段初始化为-1.
//返回创建的邻居表.
nbr_entry_t* nt_create(){
    dat_t data;
    nbr_entry_t *out;
    int neibor_num=topology_getNbrNum();
    int myID=topology_getMyNodeID();

    filetranslate(&data);

    out=malloc(neibor_num*sizeof(nbr_entry_t));

    while(data.num--){
        if(data.info[data.num].ID0==myID){
            out[--neibor_num].nodeID=data.info[data.num].ID1;
            out[neibor_num].nodeIP=data.info[data.num].IP1;
            out[neibor_num].conn=-1;
            out[neibor_num].quality=0;
        }
        else if(data.info[data.num].ID1==myID){
            out[--neibor_num].nodeID=data.info[data.num].ID0;
            out[neibor_num].nodeIP=data.info[data.num].IP0;
            out[neibor_num].conn=-1;
            out[neibor_num].quality=0;
        }
    }

    return out;
}

//这个函数删除一个邻居表. 它关闭所有连接, 释放所有动态分配的内存.
void nt_destroy(nbr_entry_t* nt)
{
    int i=0;
    int num=malloc_usable_size(nt)/sizeof(nbr_entry_t);

    while(i++<num){
        if(nt[i].conn>=0)close(nt[i].conn);
    }

    free(nt);

    return;
}

//这个函数为邻居表中指定的邻居节点条目分配一个TCP连接. 如果分配成功, 返回1, 否则返回-1.
int nt_addconn(nbr_entry_t* nt, int nodeID, int conn)
{
  return 0;
}
