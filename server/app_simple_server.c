//�ļ���: server/app_simple_server.c

//����: ���Ǽ򵥰汾�ķ������������. �������������ӵ�����SIP����. Ȼ��������stcp_server_init()��ʼ��STCP������.
//��ͨ�����ε���stcp_server_sock()��stcp_server_accept()����2���׽��ֲ��ȴ����Կͻ��˵�����. ������Ȼ����������������ӵĿͻ��˷��͵Ķ��ַ���.
//���, ������ͨ������stcp_server_close()�ر��׽���, ���Ͽ��뱾��SIP���̵�����.

//��������: 2015��

//����: ��

//���: STCP������״̬

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <netdb.h>
#include "../common/constants.h"
#include "stcp_server.h"
#include "../topology/topology.h"

//������������, һ��ʹ�ÿͻ��˶˿ں�87�ͷ������˿ں�88. ��һ��ʹ�ÿͻ��˶˿ں�89�ͷ������˿ں�90.
#define CLIENTPORT1 87
#define SERVERPORT1 88
#define CLIENTPORT2 89
#define SERVERPORT2 90
//�ڽ��յ��ַ�����, �ȴ�15��, Ȼ��ر�����.
#define WAITTIME 15

//int socket_fd;

//����������ӵ�����SIP���̵Ķ˿�SIP_PORT. ���TCP����ʧ��, ����-1. ���ӳɹ�, ����TCP�׽���������, STCP��ʹ�ø����������Ͷ�.
int connectToSIP() {
	//����Ҫ��д����Ĵ���.
	char myname[128];
	struct hostent *myhost;
	struct sockaddr_in server_addr;
	int sip_conn;

	gethostname(myname,sizeof(myname));
	myhost=gethostbyname(myname);
	if(NULL==myhost)return -1;

	sip_conn=socket(AF_INET,SOCK_STREAM,0);
	if(sip_conn==-1)
	{
		printf("Socket Error:%s\a\n",strerror(errno));
		return -1;
	}

	bzero(&server_addr,sizeof(server_addr));
	server_addr.sin_family=AF_INET;
	server_addr.sin_port=htons(SIP_PORT);
	server_addr.sin_addr=*((struct in_addr*)(myhost->h_addr_list[0]));

	if(connect(sip_conn,(struct sockaddr *)&server_addr,sizeof(struct sockaddr))==-1)
	{
		printf("Connect Error:%s\a\n",strerror(errno));
		return -1;
	}

	printf("TCP connected!\n");

	return sip_conn;
}

//��������Ͽ�������SIP���̵�TCP����.
void disconnectToSIP(int sip_conn) {
	//����Ҫ��д����Ĵ���.
    close(sip_conn);
    //close(socket_fd);
}

int main() {
	//���ڶ����ʵ����������
	srand(time(NULL));
	topology_init();

	//���ӵ�SIP���̲����TCP�׽���������
	int sip_conn = connectToSIP();
	if(sip_conn<0) {
		printf("can not connect to the local SIP process\n");
	}
	//int sip_conn = 5;

	//��ʼ��STCP������
	stcp_server_init(sip_conn);

	//�ڶ˿�SERVERPORT1�ϴ���STCP�������׽���
	int sockfd= stcp_server_sock(SERVERPORT1);
	int sockfd_my;
	if(sockfd<0) {
		printf("can't create stcp server\n");
		exit(1);
	}
	//��������������STCP�ͻ��˵�����
	sockfd_my=stcp_server_accept(sockfd);

	//�ڶ˿�SERVERPORT2�ϴ�����һ��STCP�������׽���
	int sockfd2= stcp_server_sock(SERVERPORT2);
	int sockfd_my2;
	if(sockfd2<0) {
		printf("can't create stcp server\n");
		exit(1);
	}
	//��������������STCP�ͻ��˵�����
	sockfd_my2=stcp_server_accept(sockfd2);

	char buf1[6];
	char buf2[7];
	int i;
	//�������Ե�һ�����ӵ��ַ���
	for(i=0;i<5;i++) {
		stcp_server_recv(sockfd_my,buf1,6);
		printf("recv string: %s from connection 1\n",buf1);
	}
	//�������Եڶ������ӵ��ַ���
	for(i=0;i<5;i++) {
		stcp_server_recv(sockfd_my2,buf2,7);
		printf("recv string: %s from connection 2\n",buf2);
	}

	sleep(WAITTIME);

	//�ر�STCP������
	if(stcp_server_close(sockfd_my)<0) {
		printf("can't destroy stcp server\n");
		exit(1);
	}
	if(stcp_server_close(sockfd_my2)<0) {
		printf("can't destroy stcp server\n");
		exit(1);
	}
	if(stcp_server_close(sockfd)<0) {
		printf("can't destroy stcp server\n");
		exit(1);
	}
	if(stcp_server_close(sockfd2)<0) {
		printf("can't destroy stcp server\n");
		exit(1);
	}

	//�Ͽ���SIP����֮�������
	disconnectToSIP(sip_conn);

	return 0;
}
