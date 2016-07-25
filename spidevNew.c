/*
 * SPI testing utility (using spidev driver)
 *
 * Copyright (c) 2007  MontaVista Software, Inc.
 * Copyright (c) 2007  Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * Cross-compile with cross-gcc -I/path/to/cross-kernel/include
 */

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
//#include <linux/delay.h>


#include "spidev.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static void pabort(const char *s)
{
	perror(s);
	abort();
}

//static const char *device = "/dev/spidev1.1";
static const char *device = "/dev/spidev1.0";
static uint8_t mode = 0;
static uint8_t bits = 8;
static uint32_t speed = 800000;
static uint8_t par0 = 0;
static uint8_t regAdd = 0;
static uint8_t par2 = 0;
static uint8_t par3 = 0;
static uint8_t parlen = 4;
#define spi_read  3
#define spi_write  2


#define WRITE  0 
static void transfer(int fd)
{
	int ret;
	int i = 10000;
	uint8_t tx[] = {
		spi_write, 0x0b, 0x33, 0x00, 0x01, 0x02,
	};
	if( par0 == 2)
	{
		tx[0] =par0;
		tx[2] =par2;
		tx[3] =par3;
	}
	else if( par0 == 3){
		parlen = 2;
		tx[0] = par0;
	}
		
	tx[1] =regAdd;
	uint8_t rx[ARRAY_SIZE(tx)] = {0, };
	struct spi_ioc_transfer tr_txrx[] = {
		{
                .tx_buf = (unsigned long)tx,
                .rx_buf = 0,
                .len = parlen,
                .delay_usecs = 0,
                .speed_hz = speed,
                .bits_per_word = bits,
                .chip_sel  = 1, 
		},
		{
                .tx_buf = 0,
                .rx_buf = (unsigned long)rx,
                .len = 2,
                .delay_usecs = 0,
                .speed_hz = speed,
                .bits_per_word = bits,
                .chip_sel  = 1, 
		},
		{
                .tx_buf = (unsigned long)(tx+1),
                .rx_buf = 0,
                .len = 4,
                .delay_usecs = 800,
                .speed_hz = speed,
                .bits_per_word = bits,
                .chip_sel  = 2, 
		},
	};
	if( par0 == spi_write)
        ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr_txrx[0]);
	else if( par0 == spi_read)
		ret = ioctl(fd, SPI_IOC_MESSAGE(2), &tr_txrx[0]);
	else
		return;
	
        if (ret == 1) {
                pabort("can't revieve spi message");
		}
	printf("send:  ");

	for (ret = 0; ret < tr_txrx[0].len; ret++) {
		printf("%.2X ", tx[ret]);
	}

	if( par0 == spi_read){
		printf("\nrecv:  ");

		for (ret = 0; ret < tr_txrx[1].len; ret++) {
			if (!(ret % 6))
				puts("");
			printf("%.2X ", rx[ret]);
		}
	puts("");
	}
}

void print_usage(const char *prog)
{
	printf("Usage: %s [-DsbdlHOLC3]\n", prog);
	puts("  -D --device   device to use (default /dev/spidev1.0)\n"
	     "  -s --speed    max speed (Hz)\n"
	     "  -r --regAdd    regAdd\n"
	     "  -b --bpw      bits per word \n"
	     "  -l --loop     loopback\n"
	     "  -H --cpha     clock phase\n"
	     "  -O --cpol     clock polarity\n"
	     "  -L --lsb      least significant bit first\n"
	     "  -C --cs-high  chip select active high\n"
	     "  -3 --3wire    SI/SO signals shared\n");
	exit(1);
}

void parse_opts(int argc, char *argv[])
{
	while (1) {
		static const struct option lopts[] = {
			{ "device",  1, 0, 'D' },
			{ "speed",   1, 0, 's' },
			{ "par0",   1, 0, 'c' },
			{ "regAdd",   1, 0, 'r' },
			{ "par2",   1, 0, 'e' },
			{ "par3",   1, 0, 'f' },
			{ "bpw",     1, 0, 'b' },
			{ "loop",    0, 0, 'l' },
			{ "cpha",    0, 0, 'H' },
			{ "cpol",    0, 0, 'O' },
			{ "lsb",     0, 0, 'L' },
			{ "cs-high", 0, 0, 'C' },
			{ "3wire",   0, 0, '3' },
			{ "no-cs",   0, 0, 'N' },
			{ "ready",   0, 0, 'R' },
			{ NULL, 0, 0, 0 },
		};
		int c;

		c = getopt_long(argc, argv, "c:e:f:D:s:r:b:lHOLC3NR", lopts, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'D':
			device = optarg;
			break;
		case 's':
			speed = atoi(optarg);
			break;
		case 'c':
			par0 = atoi(optarg);
			break;
		case 'r':
			regAdd = atoi(optarg);
			break;
		case 'e':
			par2 = atoi(optarg);
			parlen = 4;
			break;
		case 'f':
			par3 = atoi(optarg);
			parlen = 4;
			break;
		case 'b':
			bits = atoi(optarg);
			break;
		case 'l':
			mode |= SPI_LOOP;
			break;
		case 'H':
			mode |= SPI_CPHA;
			break;
		case 'O':
			mode |= SPI_CPOL;
			break;
		case 'L':
			mode |= SPI_LSB_FIRST;
			break;
		case 'C':
			mode |= SPI_CS_HIGH;
			break;
		case '3':
			mode |= SPI_3WIRE;
			break;
		case 'N':
			mode |= SPI_NO_CS;
			break;
		case 'R':
			mode |= SPI_READY;
			break;
		default:
			print_usage(argv[0]);
			break;
		}
	}
}


/*
 * 示例程序为读MX25L1635E spiflash的id功能
*/

//  ./spidevWrite  -r 11 -c 3
//  ./spidevWrite  -r 11 -c 2 -e 21 -f 21
int main(int argc, char *argv[])
{
	int ret = 0;
	int rst = 1,dreq;
	int fd;

	parse_opts(argc, argv);

	fd = open(device, O_RDWR);
	if (fd < 0)
		pabort("can't open device");

	/*
	 * spi mode
	 
	ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
	if (ret == -1)
		pabort("can't set wr spi mode");*/

	ret = ioctl(fd, SPI_IOC_RD_MODE, &mode);
	if (ret == -1)
		pabort("can't get spi mode");

	printf("RD_MODE: %x\n", mode);

	/*
	 * bits per word
	 
	ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't set bits per word");*/

	ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't get bits per word");

	printf("bits: %x\n", bits);

/*	rst = 0;
	ret = ioctl(fd, SPI_IOC_WR_RST, &rst);
	if (ret == -1)
		pabort("can't rst error");

	usleep( 500 );

	rst = 1;
	ret = ioctl(fd, SPI_IOC_WR_RST, &rst);
	if (ret == -1)
		pabort("can't rst 0 error");

	usleep( 2000 );*/

	ret = ioctl(fd, SPI_IOC_RD_DREQ, &dreq);
	if (ret == -1)
		pabort("can't rst 0 error");

	printf("dreq value: %x\n", dreq);


	/*
	 * max speed hz
	 
	ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("can't set max speed hz");*/

	ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("can't get max speed hz");

	printf("max speed: %d Hz (%d KHz)\n", speed, speed/1000);
	sleep (1);
	transfer(fd);

	close(fd);

	return ret;
}
