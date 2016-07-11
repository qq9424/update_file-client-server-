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


#define PRINT_COUNT   20

#define FIFO_NAME "/tmp/my_fifo"  
#define BUFFER_SIZE 6  

#define HEARTBEAT_UDP_PORT       6347 
#define HEARTBEAT_LEN			 5 


/*
int main(void)
{ 
  int pipe_fd;  
    int res;  
    int open_mode = O_WRONLY;  
    int wrCount = 20;
    char buffer[BUFFER_SIZE + 1] = "heart";  
	printf("Process %d start\n", getpid());
        fflush(stdout);
  
    if (access(FIFO_NAME, F_OK) == -1)  
    {  
	 printf("Process %d access ok\n", getpid());
        fflush(stdout);  
      res = mkfifo(FIFO_NAME, 0777);  
        if (res != 0)  
        {  
            fprintf(stderr, "Could not create fifo %s\n", FIFO_NAME);  
            exit(EXIT_FAILURE);  
        }  
    }  

    pipe_fd = open(FIFO_NAME, open_mode );  
    fprintf(stderr,"Process %d result %d\n", getpid(), pipe_fd);  
	fflush(stdout);	  
   //sleep(20);
    if (pipe_fd != -1)  
    {  
        while (wrCount--)  
        {  
            res = write(pipe_fd, buffer, BUFFER_SIZE);  
            if (res == -1)  
            {  
                fprintf(stderr, "Write error on pipe\n");  
				fflush(stdout);
                exit(EXIT_FAILURE);  
            }  
            sleep(1);
          fprintf(stderr,"client send: %sd:%d\n",buffer,wrCount);
	fflush(stdout);
        }  
        close(pipe_fd);  
    }  
    else  
    {  
        printf("error pipe_fd %d finish\n", pipe_fd);  
        exit(EXIT_FAILURE);  
    }  
  
    printf("Process %d finish\n", getpid());  
    exit(EXIT_SUCCESS); 
    
    
  return(0); 
}*/
int selectWait(int fd ){
	int ret;  
	fd_set readfds;  
	struct timeval timeout; 
	timeout.tv_sec = 1;			 
	timeout.tv_usec = 100;  
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

int main()
{  
	
	struct sockaddr_in	 client_addr;  
	socklen_t addr_len;
	struct UPDATE_CMD *comm;
	int ret;
	
	bzero(&client_addr, sizeof(client_addr));  
	client_addr.sin_family = AF_INET;  
	client_addr.sin_addr.s_addr = inet_addr( "127.0.0.1" );  
	client_addr.sin_port = htons(HEARTBEAT_UDP_PORT);  
	
	
	struct sockaddr_in	 server_addr;  
	bzero(&server_addr, sizeof(server_addr));  
	server_addr.sin_family = AF_INET;  
	server_addr.sin_addr.s_addr = htons( "127.0.0.1" );   
	server_addr.sin_port = htons(0); 
 
	int socketFD = socket(PF_INET, SOCK_DGRAM , 0);  
	if (socketFD < 0)  
	{  
		printf("HT Create Socket Failed!\n");	
		exit(1);  
	}  

	if (bind(socketFD, (struct sockaddr*)&server_addr, sizeof(server_addr)))  
	{  
		printf("HT Server Bind Port Failed!\n");  
		exit(1);  
	}  
	
	int i=1, retry = 0;
 
	
   do{	
   		
		char buffer[HEARTBEAT_LEN] = "HTBT";  
   		sleep(1);
   		addr_len=sizeof(client_addr);
		if( (i = sendto(socketFD,buffer,HEARTBEAT_LEN,0,(struct sockaddr*)&client_addr,addr_len) ) < 0)
		{
		  perror("HT err sendrto");
		}
		printf("HT SEND\n");  
		ret = selectWait( socketFD );
		if(ret > 0)
		{
			ret = recvfrom(socketFD,buffer,HEARTBEAT_LEN,0,(struct sockaddr*)&client_addr,&addr_len) ; 
			printf("HT RECV:%s\n",buffer);
		}
		else
			continue;
	}while( 1 );
	
	close(socketFD);  
  
	return 0;  
}




                                                                                         
