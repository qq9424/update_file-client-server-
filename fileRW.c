#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

void main(int argc, char *argv[])
{
	int fd;
	int fdw;
	int count = 10;
	char buff[101];
	int iCount,i;
	buff[100] = '\0'; 
	if( argc > 1)
		count =  atoi(argv[1]);
	printf(" read 1.mp3, write to 1back.mp3, repeat num:%d\n", count);

	for( i = 0; i < count; i++)
	{
		fd = open("1.mp3", O_RDONLY);
		if( fd == -1 )
	     {
	          printf("open 1.mp3 Error\n");
	          return; 
	     }
		fdw = open("1back.mp3",O_RDWR | O_TRUNC | O_CREAT);
		if( fdw == -1 )
	     {
	          printf("open 1back.mp3  Error\n");
	          return; 
	     }
		
		do{
		
			iCount = read(fd, buff, 100);
			 if( iCount == -1 )
		     {
		          printf("read Error\n");
		          return; 
		     }
			write(fdw,buff,iCount);
			
		}while(iCount);
		
		printf("r w num:%d\n", i);
		close(fd);
		close(fdw);
		sleep(1);
		
	}
}

