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

#define PRINT_COUNT   20

#define FIFO_NAME "/tmp/my_fifo"  
#define BUFFER_SIZE 6  


int main(void)
{ 
  int pipe_fd;  
    int res;  
    int open_mode = O_WRONLY;  

    char buffer[BUFFER_SIZE + 1] = "heart";  
  
    if (access(FIFO_NAME, F_OK) == -1)  
    {  
        res = mkfifo(FIFO_NAME, 0777);  
        if (res != 0)  
        {  
            fprintf(stderr, "Could not create fifo %s\n", FIFO_NAME);  
            exit(EXIT_FAILURE);  
        }  
    }  

    pipe_fd = open(FIFO_NAME, open_mode);  
    printf("Process %d result %d\n", getpid(), pipe_fd);  
  
   //sleep(20);
    if (pipe_fd != -1)  
    {  
        while (1)  
        {  
            res = write(pipe_fd, buffer, BUFFER_SIZE);  
            if (res == -1)  
            {  
                fprintf(stderr, "Write error on pipe\n");  
                exit(EXIT_FAILURE);  
            }  
            sleep(1);
          //printf("client send: %s\n",buffer);
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
}



                                                                                         