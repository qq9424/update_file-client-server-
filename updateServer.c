#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <fcntl.h>  
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>   
#include <sys/socket.h>   
#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <sys/ioctl.h>
#include<linux/tcp.h>
#include <signal.h>
#include <pthread.h>

#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#include <stdio.h>


#define UDP_PORT       6327   

#define HEARTBEAT_UDP_PORT       6347 

#define INFO_FILE  "XtInFO.dat"

             
#define MAX_FILE_LEN 64
#define LENGTH_OF_LISTEN_QUEUE     20  

#define LOG_COUNT     	1000
#define ERR_COUNT     	8
#define RECV_TIMEOUT    10


#define PRO_VERSION  0X0105

#define CMD_ADDRESS_REQ      0X3053            //��ַ����
#define CMD_ADDRESS_RSP      0X2054 		   //��ַ������Ӧ:�ն�

#define CMD_UPDATE_REQ      	0X2063             //���ļ���������:�ն�
#define CMD_UPDATE_REQ_OTHER    0X2064             //�����ļ���������:�ն�

#define CMD_UPDATE_YES      0X3064 		   //�ļ�������Ӧ:���ļ�����
#define CMD_UPDATE_NO       0X3065 		   //�ļ�������Ӧ:���ļ�����
#define CMD_UPDATE_START    0X2066 		   //�ļ����¿�ʼ:�ն�
#define CMD_UPDATE_FILE     0X3067 		   //�ļ�������

#define CMD_ERR     		   0X3073            //�������


struct UPDATE_CMD{
	short Cmd;
	short DataLen;               //����С���ļ������ã�ע�� CMD_UPDATE_REQ����ʱΪ����汾��
	union{
		int Port;           //CMD_ADDRESS_REQ: ��ַ����ʱ�����·���������TCP����ͨ�Ŷ˿�
		int Flen;           //CMD_UPDATE_YES:  �ļ��ܴ�С���ļ�������Ӧʱ��
		int TM;             //CMD_UPDATE_FILE: �����룬�ļ����¹�������
	}uni;
};

#define BUFFER_SIZE               (1024+sizeof(struct UPDATE_CMD))    

#define RING_BUFFER_SIZE               ( 4 * (1024+sizeof(struct UPDATE_CMD)) )   //ring buffer, to process tcp merge send 

int TCP_DATA_PORT =  6327 ;


#define FIFO_NAME "/tmp/my_fifo"  
//#define KILL_PROGRAM  " kill -9 `ps  | grep AVHCPG  | awk '{print $1}'`"
#define PROGRAM_NAME  "/avh/AVHCPG  -qws&"
//#define PROGRAM_NAME_BACK  "/avh/avhProgramBack"

struct sockaddr_in update_server_addr, connect_server_addr;  //���·�������Ϣ  

pthread_t sig_user_pid, addressResponse_pid;

#define  READ_BUF_SIZE  256

char info_version[16];
char info_serial[16];
char info_machineNo[16];

long find_pid_by_name( char* pidName)
{
    DIR *dir;
    struct dirent *next;
    int i=0;
 
        ///proc中包括当前的进程信息,读取该目录
    dir = opendir("/proc");
    if (!dir){
        printf("Cannot open /proc");
	return 0;
	}
     
        //遍历
    while ((next = readdir(dir)) != NULL) {
        FILE *status;
        char filename[READ_BUF_SIZE];
        char buffer[READ_BUF_SIZE];
        char name[READ_BUF_SIZE];
 
        /* Must skip ".." since that is outside /proc */
        if (strcmp(next->d_name, "..") == 0)
            continue;
 
        /* If it isn't a number, we don't want it */
        if (!isdigit(*next->d_name))
            continue;
                //设置进程
        sprintf(filename, "/proc/%s/status", next->d_name);
        if (! (status = fopen(filename, "r")) ) {
            continue;
        }
        if (fgets(buffer, READ_BUF_SIZE-1, status) == NULL) {
            fclose(status);
            continue;
        }
        fclose(status);
 
                //得到进程id
        /* Buffer should contain a string like "Name:   binary_name" */
        sscanf(buffer, "%*s %s", name);
        if (strstr(name, pidName) != NULL ) {
            return strtol(next->d_name, NULL, 0);
        }
    }
 
    return 0;
}

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


static int updateFile(  )
{  
    char  FileFullName[MAX_FILE_LEN]="";
	int retValue = -1;
	int bmainProgram = 1;
    struct UPDATE_CMD *comm;
	char buffer[RING_BUFFER_SIZE];  
	char *pRingBuff;
	int RingBuffSize;
	int ringHead = 0, ringTail = 0;
	int ringLeft = RING_BUFFER_SIZE;
	struct UPDATE_CMD cmdHead;
	
	int value = 1,recvLen = 0, fileLen = 0, TM = -1,recFileLen = 0,ret;
	int minCmdLen = sizeof(struct UPDATE_CMD);
	printf("Update Server program started!\n");  
	int client_socket = socket(AF_INET, SOCK_STREAM, 0);  
	if (client_socket < 0)	
	{  
		printf("Create Socket Failed!\n");	
		return -1;  
	}  

	//while( 1 )
		{
		//if( trigUpdateRequest ) 
			{

		printf("updateFile catch trig\n");  
		struct sockaddr_in local_addr;  
	    bzero(&local_addr, sizeof(local_addr));  
	    local_addr.sin_family = AF_INET; 
	    local_addr.sin_addr.s_addr = htons(INADDR_ANY); 
	    local_addr.sin_port = htons(0); 
	  
	    
		setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (void *)&value,sizeof(value));
		
	    if (bind(client_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)))  
	    {  
	        printf("Client Bind Port Failed!\n"); 
			goto exitPro;
	    }  

		    struct sockaddr_in  server_addr;  
		    bzero(&server_addr, sizeof(server_addr));  
		    server_addr.sin_family = AF_INET;  
		  
		    server_addr.sin_addr = update_server_addr.sin_addr;
		    server_addr.sin_port = htons(TCP_DATA_PORT);  
		    socklen_t server_addr_length = sizeof(server_addr);  

			
			printf("updateFile port :%d,client add: %s\n",TCP_DATA_PORT, inet_ntoa(update_server_addr.sin_addr) );
		  
		    if (connect(client_socket, (struct sockaddr*)&server_addr, server_addr_length) < 0)  
		    {  
		        printf("Can Not Connect To !\n");  
				goto exitPro; 
		    } 
			printf("buffer add start:%x, end:%x\n", (void *)buffer, (void *)&buffer[RING_BUFFER_SIZE]);

regetFile:
			TM = -1;
			recFileLen = 0;
			fileLen = 0;
		  	comm = (struct UPDATE_CMD *) buffer;

		    bzero(buffer, RING_BUFFER_SIZE); 
		    
			comm->DataLen = PRO_VERSION;

			char *pbuf;
    
			pbuf = buffer + sizeof( struct UPDATE_CMD );

			if( bmainProgram ){
				comm->Cmd = CMD_UPDATE_REQ;
				* pbuf = strlen ( info_version ) + 1;
				strcpy( ++pbuf , info_version );
				pbuf += strlen ( info_version ) + 1;

				* pbuf = strlen ( info_serial ) + 1;
				strcpy( ++pbuf , info_serial );
				pbuf += strlen ( info_serial ) + 1;
				
			}
			else
				comm->Cmd = CMD_UPDATE_REQ_OTHER;
	
		    send(client_socket, buffer, pbuf-buffer, 0);  

			ret = selectWait( client_socket );
	        if(ret > 0)  
				recvLen = recv(client_socket, buffer, BUFFER_SIZE, 0);  
			else
				goto exitPro;
			
			if (recvLen >= minCmdLen ) {
				if( comm->Cmd == CMD_UPDATE_YES){  
					fileLen = comm->uni.Flen;
					if( comm->DataLen < MAX_FILE_LEN ) {
						strncpy(FileFullName ,buffer + minCmdLen,comm->DataLen); //ָ���ļ�Ŀ¼��
						FileFullName[comm->DataLen] = 0;
						comm->Cmd = CMD_UPDATE_START;
						printf("File need to update!fileName:%s,fileLen:%d\n", FileFullName, fileLen);  
					}
					send(client_socket, buffer, minCmdLen, 0); 
		    	}
				else if( (comm->Cmd == CMD_UPDATE_NO) && bmainProgram){  
		       		printf("No main file need to update!\n");  
					retValue = 0;
					bmainProgram = 0;
					goto regetFile;
		    	}
				else {  
					if (comm->Cmd == CMD_UPDATE_NO)
						printf("no file need update!\n");  
					else
		       			printf("Error response!\n");  
					goto exitPro;
		    	}
			}
			else 
			{  
				printf("Server Recieve len:%d , exit update!\n", recvLen);  
				goto exitPro; 
			} 
			
		    bmainProgram = 0;  
			FILE *fp = fopen(FileFullName, "wb"); 
		 //   FILE *fp = fopen("/avh/abc", "wb");  
		    if (fp == NULL)  
		    {  
		        printf("File:\t%s create error!\n", FileFullName);  
		    }  
		    else  
		    {  
		        // �ӷ������˽������ݵ�buffer��   
		        bzero(buffer, sizeof(buffer));  
				ringHead = 0;
				ringTail = 0;
				pRingBuff = buffer;
		        while( 1 )  
		        {  
		        	ret = selectWait( client_socket );

					//head == tail : buffer is null
					if( ringHead >= ringTail) //����ʣ��ռ�
					{
						if( 0 == ringTail) //����headָ����tailָ���غ�
							ringLeft = RING_BUFFER_SIZE - ringHead - 1;//���洢�ռ�ΪRING_BUFFER_SIZE - 1
						else
							ringLeft = RING_BUFFER_SIZE - ringHead;
						
					}
					else
						ringLeft = ringTail - ringHead - 1;
					
			        if(ret > 0)  
						recvLen = recv(client_socket, buffer + ringHead, ringLeft, 0);  
					else{
						printf("selectWait err:%d, errno:%d,RingBuffSize:%d \n", ret , errno, RingBuffSize); 
						fclose(fp); 
						retValue = 1;
						goto regetFile;
					}
		            if (recvLen <= 0)  
		            {  
		                printf("Recieve Data From Server %s over!\n", inet_ntoa(server_addr.sin_addr));  
		                break;  
		            }

					ringHead = ( ringHead + recvLen ) % RING_BUFFER_SIZE;
					
ProcessAll:					
					RingBuffSize = (ringHead >= ringTail) ? (ringHead - ringTail) : ( ringHead + RING_BUFFER_SIZE - ringTail );
					if( RingBuffSize < minCmdLen )
					{  
					//	printf("<cmdLen\n");
		                continue;  
		            }

					int copyCount = minCmdLen;
					if(RING_BUFFER_SIZE - ringTail >= copyCount) // ������ĩβ���㹻�ռ�
				    {
				    	memcpy( (void *)&cmdHead,(void *) (buffer + ringTail), copyCount);
				        
				    }
				    else // ������ĩβ�ռ䲻���������θ���
				    {
				    	printf("cmd ring end:%d\n",ringTail );
				        memcpy( (void *)&cmdHead,      (void *) (buffer + ringTail),       RING_BUFFER_SIZE - ringTail );
						memcpy( ((char *)&cmdHead) + RING_BUFFER_SIZE - ringTail, (void *) buffer ,copyCount - (RING_BUFFER_SIZE - ringTail) );
				    }

					if( RingBuffSize < minCmdLen + cmdHead.DataLen )
					{  
					//	printf("<DataLen\n");
		                continue;  
		            }
					
					ringTail = (ringTail + minCmdLen) % RING_BUFFER_SIZE;
	
					if( cmdHead.Cmd == CMD_UPDATE_FILE)
					{
						copyCount = cmdHead.DataLen;
						if(RING_BUFFER_SIZE - ringTail >= copyCount) // ������ĩβ���㹻�ռ�
					    {
							fwrite( (void *) (buffer + ringTail), sizeof(char), copyCount, fp);  
					    }
					    else // ������ĩβ�ռ䲻���������θ���
					    {
					    	fwrite( (void *) (buffer + ringTail), sizeof(char), RING_BUFFER_SIZE - ringTail, fp);
							fwrite( (void *) (buffer), sizeof(char), copyCount - (RING_BUFFER_SIZE - ringTail), fp);
							//printf("write file len:%d\n",recFileLen );
					    }
						recFileLen += cmdHead.DataLen;
						
						ringTail = ( ringTail + cmdHead.DataLen ) % RING_BUFFER_SIZE;
						goto ProcessAll;
					    //printf("rec len:%d,ringTail:%d,comm add:%x\n",recvLen , ringTail , comm);  
					}
					else
						printf("err cmdHead.Cmd:%x\n",cmdHead.Cmd);
					
					if( recFileLen >= fileLen ){
							printf("receive over:recFileLen :%d, said size:%d\n", recFileLen ,fileLen );  
							break;
					}
		        }  
				retValue = 1;
				if( recFileLen == fileLen ){
		        	fclose(fp);  
		        	printf("Update File:\t %s From Server[%s] Finished,len:%d!\n", FileFullName, inet_ntoa(server_addr.sin_addr) ,recFileLen);  
				}
				else{
					printf("Update File:\t %s From Server[%s] ERROR,len:%d!\n", FileFullName, inet_ntoa(server_addr.sin_addr),recFileLen ); 
					fclose(fp);  
					//remove(FileFullName);
				}
				goto regetFile;
		    }  
		//	close(client_socket);  
		}
	//	sleep( 2 );
	}
  
 exitPro:
 	close(client_socket); 
    return retValue;  
} 

//��ַ����
void * addressResponse(void  )  
{  
    
    struct sockaddr_in   local_addr;  
	struct UPDATE_CMD *comm;
	int recvLen = 0;
	
    bzero(&local_addr, sizeof(local_addr));  
    local_addr.sin_family = AF_INET;  
    local_addr.sin_addr.s_addr = htons(INADDR_ANY);  
    local_addr.sin_port = htons(UDP_PORT);  
  
 
    int server_socket = socket(PF_INET, SOCK_DGRAM , 0);  
    if (server_socket < 0)  
    {  
        printf("Create Socket Failed!\n");  
        return;  
    }  
  
    if (bind(server_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)))  
    {  
        printf("Server Bind Port: %d Failed!\n", UDP_PORT);  
        return;  
    }  
    char buffer[BUFFER_SIZE];  
	char *pbuf;
    
	comm = (struct UPDATE_CMD *) buffer;
//	pbuf = buffer + sizeof( struct UPDATE_CMD );
	
    while(1)  
    {  
pbuf = buffer + sizeof( struct UPDATE_CMD );

        socklen_t          addr_len = sizeof(update_server_addr);  
  		printf("Address Server prepare recvfrom !\n");
        bzero(&buffer, sizeof(buffer));  
        if( (recvLen = recvfrom(server_socket,buffer,BUFFER_SIZE,0,(struct sockaddr*)&update_server_addr,&addr_len)) < 0)
        {
            perror("sendrto");
			printf("Address Server recvfrom error!\n");
            exit(-1);
        }
		printf("Address Server recvfrom !\n");
		if( recvLen >= sizeof(comm ) )
			printf("AddReq cmd: %x\n", comm->Cmd);  
		else{
			 printf("AddReq err len: %x\n", recvLen); 
		}
        
        if( comm->Cmd == CMD_ADDRESS_REQ ){
			comm->Cmd = CMD_ADDRESS_RSP;
			TCP_DATA_PORT = comm->uni.Port;
			printf("AddReq port: %d\n", comm->uni.Port);  
			* pbuf = strlen ( info_version ) + 1;
			strcpy( ++pbuf , info_version );
			pbuf += strlen ( info_version ) + 1;

			* pbuf = strlen ( info_serial ) + 1;
			strcpy( ++pbuf , info_serial );
			pbuf += strlen ( info_serial ) + 1;

			* pbuf = strlen ( info_machineNo ) + 1;
			strcpy( ++pbuf , info_machineNo );
			pbuf += strlen ( info_machineNo ) + 1;
        }
		else
			comm->Cmd = CMD_ERR;
		
		//	for( i = 0; i < ipNum; i++ )
		{
	        if(sendto(server_socket,buffer,pbuf-buffer,0,(struct sockaddr*)&update_server_addr,addr_len) < 0)
	        {
	            perror("sendrto");
	            exit(-1);
	        }
			if( comm->Cmd == CMD_ADDRESS_RSP){
				//memcpy( &(connect_server_addr.sin_addr) , &(update_server_addr.sin_addr) , sizeof(update_server_addr.sin_addr));
				printf("AddRes cmd %x, trig, connect_server_addr:%s\n", comm->Cmd , inet_ntoa(update_server_addr.sin_addr));
				//kill(sig_user_pid, SIGUSR1);
				updateFile(  );
			}
	        
    	}
    }  
  
    close(server_socket);  
  
    return (void*)0;  
}
void readInfo()
{
	char buffer[16];
	char * preturn;
	FILE *fp = fopen( INFO_FILE, "r");  
    if (fp )
    {
    	do{
	    	preturn = fgets(  buffer, sizeof(buffer), fp);  //info_version  info_serial  info_machineNo
			if( strstr(buffer , "VER:") )
				strcpy( info_version , buffer+4);
			else if( strstr(buffer , "S/N:") )
				strcpy( info_serial , buffer+4);
			else if( strstr(buffer , "No:") )
				strcpy( info_machineNo , buffer+3);
    	}while(preturn);
		printf("read ver:%s,sn:%s,no:%s",info_version, info_serial, info_machineNo);
    }
	else
    {  
        printf("File:\t%s open error!\n", INFO_FILE);  
    }  
}

int main(void)
{ 
	readInfo();
	addressResponse( ); 

/*
	if((sig_user_pid = fork()) < 0) 
      return -1 ;
    else if(sig_user_pid == 0) 
    {
      sig_user( ); 
    }*/
	
/*
    if (access(FIFO_NAME, F_OK) == -1)  
    {  
        res = mkfifo(FIFO_NAME, 0777);  
        if (res != 0)  
        {  
            fprintf(stderr, "Could not create fifo %s\n", FIFO_NAME);  
            exit(EXIT_FAILURE);  
        }  
    }  

    //syslog(LOG_INFO, "AutoStart Waiting for data form sender.");
  
    fd = open(FIFO_NAME, O_RDONLY|O_NONBLOCK, 0);
    if(fd == -1)
    {
            perror("��FIFO");
            return;
    }
int nread,errCount = 0;
	//��������⼰�ػ�ģ��
    while(1)
    {

        if((nread = read(fd, buf_r, BUFFER_SIZE)) == -1)
        {
            
            if(errno == EAGAIN) printf("û������\n");
        }
        if( nread <= 0){
          ++errCount ;
          //syslog(LOG_INFO,"AutoStart errCount��%d\n", errCount);
          printf("E��%d\n", errCount);
        }
        else {
            errCount = 0;
            if(buf_r[0]=='Q') break;
            buf_r[nread]=0;
            printf("R fifo��%s,%d\n", buf_r, nread);
        }
              
        if( ( 0 == (errCount+1) % ERR_COUNT)  ) 
        {
          errCount = 0;
		  system(KILL_PROGRAM);
          syslog(LOG_INFO, "AutoStart Time out, restart program\n");
		  usleep( 100);
          system( PROGRAM_NAME );
          printf("AutoStart Time out, restart program:%d.\n",errCount);
          sleep(5);
        }
         
        sleep(1);
    }
*/
    
    
  return(0); 
}



                                                                                         
