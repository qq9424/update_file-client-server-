#include <sys/mman.h>  
#include <unistd.h>  
#include <stdio.h>  
#include <fcntl.h>  
//#include "csapp.h"  
#include <sys/stat.h>  
#include <stdlib.h>  
#include <string.h>  
#include <errno.h>  
  

int main(int argc, char **argv)  
{  
    if (argc != 2)  
        {  
            printf("error.\n");  
            exit(0);  
        }  
        //int fd = atoi(*argv[1]);  
    //mmap()  
    int fd = open(argv[1], O_RDWR, 0);  // O_RDWR 才能被读写。  
    if (fd < 0)  
    {
    	fprintf(stderr, "open: %s\n", strerror(errno));  // 使用异常检查是个好习惯， 他可以帮助程序员迅速定位出错的地方！  
    	return 0;
    }
		char op;
		 unsigned int Addr, value;
    do
    {
	     char *bufp;  
	fflush(stdin);  
	  printf("\nr add len ,  w add data\n"); 
	   scanf("%c %x %x",&op , &Addr, &value);
	    //Pin Control PINCTRL 0x80018000 0x80019FFF     MX28_SOC_IO_PHYS_BASE	0x80000000
	    //0x8001A000 0x8001BFFF 8KB             MX28_SOC_IO_VIRT_BASE	0xF0000000      
	    //SSP0_PHYS_ADDR		(MX28_SOC_IO_PHYS_BASE + 0x010000)
	    //SSP1_PHYS_ADDR		(MX28_SOC_IO_PHYS_BASE + 0x012000)
      //SSP2_PHYS_ADDR		(MX28_SOC_IO_PHYS_BASE + 0x014000)
	printf("read data: %c %x %x\n", op, Addr, value);
      if(op == 'r' )
      {
	    	bufp = mmap((void *)Addr, value, PROT_READ , MAP_SHARED, fd, 0);  
	    	if (bufp == (void *)-1)  
	    		fprintf(stderr, "mmap: %s\n", strerror(errno));  
	    	int i = 0;
		for(; i < value; i++)
		{
	    		printf("%02x  ",*bufp++);
			if((i+1) % 4 == 0)
				 printf("\n");
		}
	    		
	    	munmap(bufp, value); 
	  	}
	    else  if(op == 'w' )
	    {
	    	bufp = mmap((void * )Addr, 4,  PROT_WRITE, MAP_SHARED, fd, 0);  
	    	if (bufp == (void *)-1)  
	    		fprintf(stderr, "mmap: %s\n", strerror(errno));  
	    	
	    	
	    	*((int *)bufp) = value;
	    		
	    	munmap(bufp, 4); 
	  }
	  else  if(op == 'x' )
	    	break;
    }while(1);  
    
    close(fd);  
    exit(0);  
} 
