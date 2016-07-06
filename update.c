//////////////////////////////////////////////////////  
// file_client.c  socket传输文件的client端示例程序   
// ///////////////////////////////////////////////////  
#include<netinet/in.h>                         // for sockaddr_in  
#include<sys/types.h>                          // for socket  
#include <arpa/inet.h>
#include<sys/socket.h>                         // for socket  
#include<stdio.h>                              // for printf  
#include<stdlib.h>                             // for exit  
#include<string.h>                             // for bzero  
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include<linux/tcp.h>

#define LENGTH_OF_LISTEN_QUEUE     255  

#define UDP_PORT       6327     
             
#define TCP_DATA_PORT       6328 
#define NEW_VERSION  		0X0106
#define FILE_SAVE_PATH      "/avh/AVHCPGtry"
#define FILE_LOCAL          "AVHCPG"
#define RECV_TIMEOUT    10


#define CMD_ADDRESS_REQ      0X3053            //地址请求
#define CMD_ADDRESS_RSP      0X2054 		   //地址请求响应:终端

#define CMD_UPDATE_REQ      0X2063             //文件更新请求:终端
#define CMD_UPDATE_YES      0X3064 		   //文件更新响应:有文件更新
#define CMD_UPDATE_NO       0X3065 		   //文件更新响应:无文件更新
#define CMD_UPDATE_START    0X2066 		   //文件更新开始:终端
#define CMD_UPDATE_FILE     0X3067 		   //文件更新中

#define CMD_ERR     		   0X3073            //命令出错


struct UPDATE_CMD{
	short Cmd;
	short DataLen;               //包大小，文件更新用（注： CMD_UPDATE_REQ命令时为软件版本）
	union{
		int Port;           //CMD_ADDRESS_REQ: 地址发现时，更新服务器发送TCP数据通信端口
		int Flen;           //CMD_UPDATE_YES:  文件总大小，文件更新响应时用
		int TM;             //CMD_UPDATE_FILE: 特征码，文件更新过程中用
	}uni;
};

#define DATA_SIZE                  1024
#define BUFFER_SIZE                (DATA_SIZE+sizeof(struct UPDATE_CMD))


enum  STATUS_UPDATE {
STATUS_UPDATE_NO  = 0,
STATUS_UPDATE_REQ = 1,
STATUS_UPDATE_YES = 2,
};

enum  STATUS_UPDATE g_bUpdateFile = STATUS_UPDATE_NO;

int selectWait(int fd ){
	int ret;  
	fd_set readfds;  
	struct timeval timeout; 
	timeout.tv_sec = RECV_TIMEOUT;			 
	timeout.tv_usec = 0;  
	FD_ZERO(&readfds); 
	FD_SET(fd, &readfds); 
	ret = select(fd+1, &readfds, NULL, NULL, &timeout);
	if(ret < 0)  
	{  
		printf("select error!\n");	
	}
	else
		ret = FD_ISSET(fd, &readfds);  
	return ret;
}

int broadcastTcpPort(  )  
{  
    
    struct sockaddr_in   client_addr;  
    socklen_t addr_len;
	struct UPDATE_CMD *comm;
	int ret;
    
    bzero(&client_addr, sizeof(client_addr));  
    client_addr.sin_family = AF_INET;  
    client_addr.sin_addr.s_addr = inet_addr( "255.255.255.255" );  
    client_addr.sin_port = htons(UDP_PORT);  
    addr_len=sizeof(client_addr);
    
    struct sockaddr_in   server_addr;  
    bzero(&server_addr, sizeof(server_addr));  
    server_addr.sin_family = AF_INET;  
    server_addr.sin_addr.s_addr = htons(INADDR_ANY);   
    server_addr.sin_port = htons(0); 
 
    int broadcast_socket = socket(PF_INET, SOCK_DGRAM , 0);  
    if (broadcast_socket < 0)  
    {  
        printf("Create Socket Failed!\n");  
        exit(1);  
    }  

    if (bind(broadcast_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)))  
    {  
        printf("Server Bind Port: %d Failed!\n", UDP_PORT);  
        exit(1);  
    }  
    
    int i=1, retry = 0;
    socklen_t len = sizeof(i);
    setsockopt(broadcast_socket,SOL_SOCKET,SO_BROADCAST,&i,len);
 
    char buffer[BUFFER_SIZE];  
    bzero(&buffer, sizeof(buffer));  

	comm = (struct UPDATE_CMD *) buffer;
	comm->Cmd = CMD_ADDRESS_REQ;
	comm->uni.Port = TCP_DATA_PORT;
    
   do{  
      if( (i = sendto(broadcast_socket,buffer,sizeof(struct UPDATE_CMD),0,(struct sockaddr*)&client_addr,addr_len) ) < 0)
      {
          perror("sendrto");
          goto exitPro;
      }
      printf("add send CMD:%x,%x,len:%d\n",comm->Cmd , comm->uni.Port, i );
	  ret = selectWait( broadcast_socket );
	  if(ret > 0)  
			ret = recvfrom(broadcast_socket,buffer,BUFFER_SIZE,0,(struct sockaddr*)&client_addr,&addr_len) ;  
	  else
			continue;
	  
      if( ret < 0)
      {
          perror("recvfrom");
          goto exitPro;
      }

      printf("add recv CMD:%x,\nclient add: %s\n",comm->Cmd , inet_ntoa(client_addr.sin_addr) );
	  if( comm->Cmd == CMD_ADDRESS_RSP )
 		 break;
	  
	  sleep(2);
	  
    }while( retry < 5 );
  exitPro:
  	
    close(broadcast_socket);  
  
    return 0;  
}

void tcpDataTransThread( void * socket)
{
	int new_server_socket = *(int *) socket;
	static int TM = 1;
	int softwareVersion = 0 , recvLen = 0, ret;
	struct UPDATE_CMD *comm;
	char buffer[BUFFER_SIZE];  

	TM++;
  	comm = (struct UPDATE_CMD *) buffer;
 	bzero(buffer, sizeof(buffer));
	
	ret = selectWait( new_server_socket );
	if(ret > 0)  
		recvLen = recv(new_server_socket, buffer, BUFFER_SIZE, 0);  
	else
		goto exitPro;

	if( recvLen >= sizeof(struct UPDATE_CMD ) )
		printf("AddReq cmd: %x\n", comm->Cmd);  
	else{
		 printf("AddReq err len: %x\n", recvLen); 
		 goto exitPro;
	}
    
    if( comm->Cmd == CMD_UPDATE_REQ ){
		softwareVersion = comm->DataLen;
		if(softwareVersion < NEW_VERSION) {
			comm->Cmd = CMD_UPDATE_YES;
		}
		else
			comm->Cmd = CMD_UPDATE_NO;
		
		printf("send cmd: %x\n", comm->Cmd);  
    }
	else
		comm->Cmd = CMD_ERR;
  
	if( comm->Cmd == CMD_UPDATE_YES) {
    	//printf("enter file full path\n"); //服务器端文件保存路径
    	//scanf("%s", buffer + sizeof(struct UPDATE_CMD ));
    	strncat(buffer + sizeof(struct UPDATE_CMD ) , FILE_SAVE_PATH , strlen( FILE_SAVE_PATH ));
		comm->DataLen = strlen( FILE_SAVE_PATH );

		unsigned long filesize = 0;	
		struct stat statbuff;
		if(stat(FILE_LOCAL, &statbuff) >= 0)
			comm->uni.Flen = statbuff.st_size;
		else
			comm->uni.Flen = 0;

	}
    
    send(new_server_socket, buffer, comm->DataLen + sizeof(struct UPDATE_CMD ), 0);  
    printf("send packet len:%d,file store:%s,fileLen:%d\n", comm->DataLen + sizeof(struct UPDATE_CMD ) , FILE_SAVE_PATH , comm->uni.Flen);

	ret = selectWait( new_server_socket );
	if(ret > 0)  
		recvLen = recv(new_server_socket, buffer, BUFFER_SIZE, 0);   
	else
		goto exitPro;

    if ( (recvLen < sizeof(struct UPDATE_CMD )) || ( comm->Cmd != CMD_UPDATE_START))  
    {  
        printf("Server Recieve UPDATE_START Failed!\n");  
        goto exitPro; 
    } 

    FILE *fp = fopen(FILE_LOCAL, "r");  
    if (fp == NULL)  
    {  
        printf("File:\t%s Can Not Open To Write!\n", FILE_LOCAL);  
        goto exitPro;  
    }  
  
     
    int file_block_length = 0;  
    while( (file_block_length = fread(buffer + sizeof(struct UPDATE_CMD ), sizeof(char), DATA_SIZE, fp)) > 0)  
    {  
        printf("file_block_length = %d\n", file_block_length);  
		comm->DataLen = file_block_length;
		comm->Cmd = CMD_UPDATE_FILE;
		comm->uni.TM = TM;
		
        if (send(new_server_socket, buffer, comm->DataLen + sizeof(struct UPDATE_CMD ), 0) < 0)  
        {  
            printf("Send File:\t%s Failed!\n", FILE_LOCAL);  
            break;  
        }  

        bzero(buffer, sizeof(buffer));  
    }  
    fclose(fp);  
    printf("File:\t%s Transfer Finished!\n", FILE_LOCAL);  
	
exitPro:
	close(new_server_socket);  
	return;  
		
}

void tcpAcceptThread(  ) 
{
	int value = 1;
		
	struct sockaddr_in	 server_addr;  
	bzero(&server_addr, sizeof(server_addr));  
	server_addr.sin_family = AF_INET;  
	server_addr.sin_addr.s_addr = htons(INADDR_ANY);  
	server_addr.sin_port = htons(TCP_DATA_PORT);  
 
	int server_socket = socket(PF_INET, SOCK_STREAM, 0);  
	if (server_socket < 0)	
	{  
		printf("Create Socket Failed!\n");	
		exit(1);  
	}  

	setsockopt(server_socket, IPPROTO_TCP, TCP_NODELAY, (void *)&value,sizeof(value));
  
	if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)))  
	{  
		printf("Server Bind Port: %d Failed!\n", TCP_DATA_PORT);  
		exit(1);  
	}  
  
	if (listen(server_socket, LENGTH_OF_LISTEN_QUEUE))	
	{  
		printf("Server Listen Failed!\n");	
		exit(1);  
	}  
	printf("tcpAcceptThread started!\n"); 

	while(1){
        struct sockaddr_in client_addr;  
		pthread_t threadId;
        socklen_t          length = sizeof(client_addr);  
  
        int new_server_socket = accept(server_socket, (struct sockaddr*)&client_addr, &length);  
        if (new_server_socket < 0)  
        {  
            printf("Server Accept Failed!\n");  
            break;  
        }  
        printf("Server Connected!\n");  

		pthread_create(&threadId,NULL, (void *)(&tcpDataTransThread), (void *)&new_server_socket);

	}
       
    close(server_socket);  

}

int main(int argc, char **argv)  
{  
	if( argc < 2){
		printf("update -t(transport file) | -b (broadcast)\n");
		return 0;
	}
	
	if( strstr(argv[1] , "-t") != NULL)
	 	tcpAcceptThread( );
	else if( strstr(argv[1] , "-b") != NULL)
    	broadcastTcpPort( ) ;
	
    return 0;  
  
} 
