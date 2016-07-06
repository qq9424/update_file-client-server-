
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>



    int main(int argc, char ** argv)
    {    
	if (argc < 2){
		printf("error: exeName num\n");
		return 0;
	}
       int fd = open("/dev/fb0",O_RDWR);
	int val = atoi(argv[1]);
	 if (fd >= 0)
        {
            	printf("open fb0 ok, val:%d\n", val);
		ioctl(fd,FBIOBLANK,val);
		close(fd);
        }
        else
        {
            printf("Big_endian\n");
        }

        return 0;
    }
