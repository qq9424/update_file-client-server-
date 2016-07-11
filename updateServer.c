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

             
#define MAX_FILE_LEN 64
#define LENGTH_OF_LISTEN_QUEUE     20  

#define LOG_COUNT     	1000
#define ERR_COUNT     	8
#define RECV_TIMEOUT    10


#define PRO_VERSION  0X0105

#define CMD_ADDRESS_REQ      0X3053            //µÿ÷∑«Î«Û
#define CMD_ADDRESS_RSP      0X2054 		   //µÿ÷∑«Î«ÛœÏ”¶:÷’∂À

#define CMD_UPDATE_REQ      0X2063             //Œƒº˛∏¸–¬«Î«Û:÷’∂À
#define CMD_UPDATE_YES      0X3064 		   //Œƒº˛∏¸–¬œÏ”¶:”–Œƒº˛∏¸–¬
#define CMD_UPDATE_NO       0X3065 		   //Œƒº˛∏¸–¬œÏ”¶:ŒﬁŒƒº˛∏¸–¬
#define CMD_UPDATE_START    0X2066 		   //Œƒº˛∏¸–¬ø™ º:÷’∂À
#define CMD_UPDATE_FILE     0X3067 		   //Œƒº˛∏¸–¬÷–

#define CMD_ERR     		   0X3073            //√¸¡Ó≥ˆ¥Ì


struct UPDATE_CMD{
	short Cmd;
	short DataLen;               //∞¸¥Û–°£¨Œƒº˛∏¸–¬”√£®◊¢£∫ CMD_UPDATE_REQ√¸¡Ó ±Œ™»Ìº˛∞Ê±æ£©
	union{
		int Port;           //CMD_ADDRESS_REQ: µÿ÷∑∑¢œ÷ ±£¨∏¸–¬∑˛ŒÒ∆˜∑¢ÀÕTCP ˝æ›Õ®–≈∂Àø⁄
		int Flen;           //CMD_UPDATE_YES:  Œƒº˛◊‹¥Û–°£¨Œƒº˛∏¸–¬œÏ”¶ ±”√
		int TM;             //CMD_UPDATE_FILE: Ãÿ’˜¬Î£¨Œƒº˛∏¸–¬π˝≥Ã÷–”√
	}uni;
};

#define BUFFER_SIZE               (1024+sizeof(struct UPDATE_CMD))

int TCP_DATA_PORT =  6327 ;


#define FIFO_NAME "/tmp/my_fifo"  
//#define KILL_PROGRAM  " kill -9 `ps  | grep AVHCPG  | awk '{print $1}'`"
#define PROGRAM_NAME  "/avh/AVHCPG  -qws&"
//#define PROGRAM_NAME_BACK  "/avh/avhProgramBack"

struct sockaddr_in update_server_addr, connect_server_addr;  //∏¸–¬∑˛ŒÒ∆˜–≈œ¢  

pthread_t sig_user_pid, addressResponse_pid;

#define  READ_BUF_SIZE  256


long find_pid_by_name( char* pidName)
{
    DIR *dir;
    struct dirent *next;
    int i=0;
 
        ///proc‰∏≠ÂåÖÊã¨ÂΩìÂâçÁöÑËøõÁ®ã‰ø°ÊÅØ,ËØªÂèñËØ•ÁõÆÂΩï
    dir = opendir("/proc");
    if (!dir){
        printf("Cannot open /proc");
	return 0;
	}
     
        //ÈÅçÂéÜ
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
                //ËÆæÁΩÆËøõÁ®ã
        sprintf(filename, "/proc/%s/status", next->d_name);
        if (! (status = fopen(filename, "r")) ) {
            continue;
        }
        if (fgets(buffer, READ_BUF_SIZE-1, status) == NULL) {
            fclose(status);
            continue;
        }
        fclose(status);
 
                //ÂæóÂà∞ËøõÁ®ãid
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

static void updateFile(  )
{  
    char  FileFullName[MAX_FILE_LEN]="";
    struct UPDATE_CMD *comm;
	char buffer[BUFFER_SIZE];  
	int value = 1,recvLen = 0, fileLen = 0, TM = -1,recFileLen = 0,ret;
	int minCmdLen = sizeof(struct UPDATE_CMD);
	printf("Update Server program started!\n");  
	int client_socket = socket(AF_INET, SOCK_STREAM, 0);  
	if (client_socket < 0)	
	{  
		printf("Create Socket Failed!\n");	
		return;  
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

		  	comm = (struct UPDATE_CMD *) buffer;

		    bzero(buffer, BUFFER_SIZE); 
		    comm->Cmd = CMD_UPDATE_REQ;
			comm->DataLen = PRO_VERSION;
		    
		    send(client_socket, buffer, minCmdLen, 0);  

			ret = selectWait( client_socket );
	        if(ret > 0)  
				recvLen = recv(client_socket, buffer, BUFFER_SIZE, 0);  
			else
				goto exitPro;
			
			if (recvLen >= minCmdLen ) {
				if( comm->Cmd == CMD_UPDATE_YES){  
					fileLen = comm->uni.Flen;
					if( comm->DataLen < MAX_FILE_LEN ) {
						strncpy(FileFullName ,buffer + minCmdLen,comm->DataLen); //÷∏œÚŒƒº˛ƒø¬º√˚
						FileFullName[comm->DataLen] = 0;
						comm->Cmd = CMD_UPDATE_START;
						printf("File need to update!fileName:%s,fileLen:%d\n", FileFullName, fileLen);  
					}
					send(client_socket, buffer, minCmdLen, 0); 
		    	}
				else if( comm->Cmd == CMD_UPDATE_NO){  
		       		printf("No file need to update!\n");  
					goto exitPro;
		    	}
				else {  
		       		printf("Error response!\n");  
					goto exitPro;
		    	}
			}
			else if( recvLen <0 )
			{  
				printf("Server Recieve Data Failed!\n");  
				goto exitPro; 
			} 
			
		       
		    FILE *fp = fopen(FileFullName, "wb");  
		    if (fp == NULL)  
		    {  
		        printf("File:\t%s create error!\n", FileFullName);  
		    }  
		    else  
		    {  
		        // ¥”∑˛ŒÒ∆˜∂ÀΩ” ’ ˝æ›µΩbuffer÷–   
		        bzero(buffer, sizeof(buffer));  
		        while( 1 )  
		        {  
		        	ret = selectWait( client_socket );
			        if(ret > 0)  
						recvLen = recv(client_socket, buffer, BUFFER_SIZE, 0);  
					else
						goto exitPro;
		            if (recvLen < 0)  
		            {  
		                printf("Recieve Data From Server %s Failed!\n", inet_ntoa(server_addr.sin_addr));  
		                break;  
		            }
					else if( recvLen < minCmdLen + comm->DataLen )
					{  
		                printf("Len check error. act:%d,expect:%d!\n", recvLen , minCmdLen + comm->DataLen);  
		                continue;  
		            }
					if( comm->Cmd == CMD_UPDATE_FILE)
					{
						if(TM == -1 )
							TM = comm->uni.TM;
						if( TM != comm->uni.TM)
						{
							printf("TM error,act:%d, expect:%d!\n", comm->uni.TM, TM );  
			                continue;  
			            }
			            printf("Server rec len:%d,TM:%d\n",recvLen , TM);  
			            int write_length = fwrite(buffer + minCmdLen, sizeof(char), comm->DataLen, fp);  
			            if (write_length < comm->DataLen)  
			            {  
			                printf("File:\t%s Write Failed!\n", FileFullName);  
			                break;  
			            }
						recFileLen += comm->DataLen;
						
					}
					if( recFileLen >= fileLen )
							break;
		            bzero(buffer, BUFFER_SIZE);  
		        }  
				if( recFileLen == fileLen ){
		        	fclose(fp);  
		        	printf("Update File:\t %s From Server[%s] Finished!\n", FileFullName, inet_ntoa(server_addr.sin_addr) );  
				}
				else{
					printf("Update File:\t %s From Server[%s] ERROR,REMOVED!\n", FileFullName, inet_ntoa(server_addr.sin_addr) ); 
					fclose(fp);  
					remove(FileFullName);
				}
		    }  
		//	close(client_socket);  
		}
	//	sleep( 2 );
	}
  
 exitPro:
 	close(client_socket); 
    return ;  
} 

//µÿ÷∑∑¢œ÷
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
    
	comm = (struct UPDATE_CMD *) buffer;
	
    while(1)  
    {  

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
        }
		else
			comm->Cmd = CMD_ERR;
		
		//	for( i = 0; i < ipNum; i++ )
		{
	        if(sendto(server_socket,buffer,recvLen,0,(struct sockaddr*)&update_server_addr,addr_len) < 0)
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


int main(void)
{ 
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
            perror("¥Úø™FIFO");
            return;
    }
int nread,errCount = 0;
	//–ƒÃ¯∞¸ºÏ≤‚º∞ ÿª§ƒ£øÈ
    while(1)
    {

        if((nread = read(fd, buf_r, BUFFER_SIZE)) == -1)
        {
            
            if(errno == EAGAIN) printf("√ª”– ˝æ›\n");
        }
        if( nread <= 0){
          ++errCount ;
          //syslog(LOG_INFO,"AutoStart errCount£∫%d\n", errCount);
          printf("E£∫%d\n", errCount);
        }
        else {
            errCount = 0;
            if(buf_r[0]=='Q') break;
            buf_r[nread]=0;
            printf("R fifo£∫%s,%d\n", buf_r, nread);
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



                                                                                         
