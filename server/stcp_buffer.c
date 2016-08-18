#include "stcp_buffer.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define DEBUG_BUF

void buffer_init(Buffer *p){
    p->p_const=malloc(BUFSIZE);
    p->p_fill=p->p_const+BUFSIZE-1;
    p->p_head=p->p_const;
    p->p_end=p->p_const;
    p->usedlen=0;
}

int in(Buffer *p,char *input,int n){
    if((BUFSIZE-p->usedlen)<n)return 1;
#ifdef DEBUG_BUF
    printf("[Buffer]in %d bytes.\n",n);
#endif
    while(n--){
        *p->p_head=*input;
        ++input;
        __sync_fetch_and_add(&p->usedlen,1);
#ifdef DEBUG_BUF
        printf("%c",*p->p_head);
#endif
        //++p->usedlen;
        if(p->p_head<p->p_fill)++p->p_head;
        else p->p_head=p->p_const;
        //if(p->p_head==p->p_end)++p->p_end;
    }
#ifdef DEBUG_BUF
    printf("\n");
#endif
    /*if(p->p_head>p->p_end)p->usedlen=p->p_head-p->p_end;
    else p->usedlen=BUFSIZE-(p->p_end-p->p_head);*/
    return 0;
}

int out(Buffer *p,char *output,int n){
    if(n==0)n=p->usedlen;
    //else if(n>BUFSIZE)n=BUFSIZE;
    //else if(n>p->usedlen)n=p->usedlen;
    while(p->usedlen<n)usleep(10);
#ifdef DEBUG_BUF
    printf("[Buffer]out %d bytes.\n",n);
#endif
    while(n--){
        *output=*p->p_end;
#ifdef DEBUG_BUF
        printf("%c",*p->p_end);
#endif
        __sync_fetch_and_sub(&p->usedlen,1);
        //--p->usedlen;
        ++output;
        if(p->p_end<p->p_fill)++p->p_end;
        else p->p_end=p->p_const;
    }
#ifdef DEBUG_BUF
    printf("\n");
#endif

    /*if(p->p_head>p->p_end)p->usedlen=p->p_head-p->p_end;
    else p->usedlen=BUFSIZE-(p->p_end-p->p_head);*/

    return n;
}

int iffull(Buffer *p,int n){
    if((BUFSIZE-p->usedlen)<n)return 1;
    else return 0;
}
