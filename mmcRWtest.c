#include <stdio.h>
#include <unistd.h> 
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define tryTime  	500
#define OPEN  	open
#define READ  	read
#define WRITE  	write
#define CLOSE  	close

//FILE *fp;
int fp;

int main(void)
{
	
	int iCount, i = tryTime  ;
	char *str = "Hello world!\n";
	char strRead[255];

	
	while(i--)
	{
		fp = OPEN ("/media/sd-mmcblk0p1/test", O_APPEND);
	if( fp == NULL)
	{
		printf("/media/sd-mmcblk0p1/test fopen fail\n");
		return -1;
	}

		iCount = write(fp,str, 12 ); //fwrite(str, 12, 1, fp);
		if(iCount <= 0)
		{
			printf("write fail,errno:%d\n",errno);
			break;
		}
		printf("fwrite errno:%d,iCount:%d\n",errno,iCount);

		usleep(100);
		iCount = read(fp, strRead, 20 ); //fread(strRead, 1, 20, fp);
		if(iCount < 0)
		{
			printf("read fail,errno:%d,iCount:%d, strRead:%s \n",errno,iCount,strRead );
			break;
		}
		printf("fread errno:%d,iCount:%d ,execute times:%d \n",errno,iCount,tryTime  - i + 1);

		usleep(100);
		CLOSE(fp);
	}
	printf("%d block(12 bytes per block) read, check it out!\n", iCount);
	
}
