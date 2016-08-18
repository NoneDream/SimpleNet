#ifndef STCP_BUFFER_H_INCLUDED
#define STCP_BUFFER_H_INCLUDED

#define     BUFSIZE 65536
#pragma pack(1)
typedef struct{
    char *p_const;//缓存头指针
    char *p_fill;//缓存尾指针（有数据）
    char *p_head;//head有数据
    char *p_end;//end无数据
    int usedlen;
}Buffer;

void buffer_init(Buffer *p);
int in(Buffer *p,char *input,int n);//写入成功返回0，失败返回1
int out(Buffer *p,char *output,int n);
int iffull(Buffer *p,int n);
#pragma pack()
#endif // STCP_BUFFER_H_INCLUDED
