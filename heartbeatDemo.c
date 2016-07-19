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
#define ERR_COUNT     	8

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


//µÿ÷∑∑¢œ÷
void * UDPHeartBeatDetect(void  )  
{  
    
    struct sockaddr_in   local_addr;  
	struct UPDATE_CMD *comm;
	int recvLen = 0;
	int errCount = 0;
	struct sockaddr_in heartBeatServer;
	
    bzero(&local_addr, sizeof(local_addr));  
    local_addr.sin_family = AF_INET;  
    local_addr.sin_addr.s_addr =  inet_addr("127.0.0.1"); //htons( INADDR_ANY );  
    local_addr.sin_port = htons(HEARTBEAT_UDP_PORT);  
  
 
    int server_socket = socket(PF_INET, SOCK_DGRAM , 0);  
    if (server_socket < 0)  
    {  
        printf("Create Socket Failed!\n");  
        return;  
    }  

  	int flags = fcntl(server_socket,F_GETFL,0);//ªÒ»°Ω®¡¢µƒsockfdµƒµ±«∞◊¥Ã¨£®∑«◊Ë»˚£©
	fcntl(server_socket,F_SETFL,flags|O_NONBLOCK);//Ω´µ±«∞sockfd…Ë÷√Œ™∑«◊Ë»˚

    if (bind(server_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)))  
    {  
        printf("Server Bind Port: %d Failed! errno:%d\n", HEARTBEAT_UDP_PORT,errno );  
        return;  
    }  
    char buffer[READ_BUF_SIZE];  
	char KILL_PROGRAM[32];
    
	comm = (struct UPDATE_CMD *) buffer;
	
    while(1)  
    {  
		sleep(1);
		if( errCount > 1)
			printf("HeartBeatDetect:%d\n",errCount);
        socklen_t          addr_len = sizeof(heartBeatServer);  

        bzero(&buffer, sizeof(buffer));  
        if( (recvLen = recvfrom(server_socket,buffer,READ_BUF_SIZE,0,(struct sockaddr*)&heartBeatServer,&addr_len)) < 0)
        {
        	if( !(errno == EAGAIN || errno == EWOULDBLOCK) )
				printf("error return :%x\n", errno );
			
			if( ( errCount >= ERR_COUNT)  ) 
	        {
	          errCount = -5;
			  long pid = find_pid_by_name("AVHCPG");
			  sprintf(KILL_PROGRAM, "kill  %d", pid);
			  if( pid )
			  	system(KILL_PROGRAM);
			  usleep( 100);
	          system( PROGRAM_NAME );
	          printf("%s\n AutoStart Time out, restart program:%d.\n",KILL_PROGRAM , errCount);
	        }
			else			
        		++errCount ;
            continue;
        }

		//receive heart beat
		errCount = 0;
        
        if(sendto(server_socket,buffer,recvLen,0,(struct sockaddr*)&heartBeatServer,addr_len) < 0)
        {
            perror("sendrto");
            exit(-1);
        }

    }  
  
    close(server_socket);  
  	printf("AutoStart exit heartbeat detect.\n");
    return (void*)0;  
}

int main(void)
{ 


	UDPHeartBeatDetect();
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



                                                                                         
