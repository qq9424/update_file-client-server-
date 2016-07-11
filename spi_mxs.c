/*
 * Freescale MXS SPI master driver
 *
 * Author: dmitry pervushin <dimka@embeddedalley.com>
 *
 * Copyright 2008-2010 Freescale Semiconductor, Inc.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <asm/dma.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>


#include <mach/regs-ssp.h>
#include <mach/dmaengine.h>
#include <mach/device.h>
#include <mach/system.h>
#include <mach/hardware.h>

#include "spi_mxs.h"
#include "../../arch/arm/mach-mx28/mx28_pins.h"
#define SPI_TIMEOUT  50


#ifndef BF_SSP_CTRL0_XFER_COUNT
#define BF_SSP_CTRL0_XFER_COUNT(len)	HW_SSP_VERSION
#endif
#ifndef BM_SSP_CTRL0_XFER_COUNT
#define BM_SSP_CTRL0_XFER_COUNT		HW_SSP_VERSION
#endif

#ifndef BF_SSP_XFER_SIZE_XFER_COUNT
#define BF_SSP_XFER_SIZE_XFER_COUNT(len)	HW_SSP_VERSION
#endif
#ifndef BM_SSP_XFER_SIZE_XFER_COUNT
#define BM_SSP_XFER_SIZE_XFER_COUNT	HW_SSP_VERSION
#endif

#ifndef HW_SSP_XFER_SIZE
#define HW_SSP_XFER_SIZE HW_SSP_VERSION
#endif

/* 0 means DMA modei(recommended, default), !0 - PIO mode */
static int pio = 1 /* = 0 */ ;
static int debug;


extern int g_readjiffies;
extern  int DReqValue ;
extern  int g_autoReadPCM ;
#define GPIO_XCS		MXS_PIN_TO_GPIO(PINID_SSP2_SS0)
#define GPIO_XDCS		MXS_PIN_TO_GPIO(PINID_LCD_D17)
#define GPIO_RST		MXS_PIN_TO_GPIO(PINID_SAIF0_LRCLK)


extern int g_readjiffies2;
extern  int DReqValue2 ;
extern  int g_autoReadPCM2 ;
#define GPIO_XCS2		MXS_PIN_TO_GPIO(PINID_SSP2_SS0)
#define GPIO_XDCS2		MXS_PIN_TO_GPIO(PINID_LCD_D17)
#define GPIO_RST2		MXS_PIN_TO_GPIO(PINID_SAIF0_LRCLK)


static spinlock_t spiLock2 = SPIN_LOCK_UNLOCKED; ;


extern void usleep(unsigned int usecs);

static spinlock_t spiLock = SPIN_LOCK_UNLOCKED; ;
static u8  chip_sel;
static u8  chip_sel2;
static  struct task_struct *spi_kthread = NULL;
static  struct task_struct *spi_kthread2 = NULL;
/**
 * mxs_spi_init_hw
 *
 * Initialize the SSP port
 */
static int mxs_spi_init_hw(struct mxs_spi *ss)
{
	struct mxs_spi_platform_data *pdata;
	int err;

	pdata = ss->master_dev->platform_data;

	if (!pdata->clk) {
		dev_err(ss->master_dev, "unknown clock\n");
		err = -EINVAL;
		goto out;
	}
	ss->clk = clk_get(ss->master_dev, pdata->clk);
	if (IS_ERR(ss->clk)) {
		err = PTR_ERR(ss->clk);
		goto out;
	}
	clk_enable(ss->clk);

	mxs_reset_block((void *)ss->regs, 0);
	mxs_dma_reset(ss->dma);

	return 0;

out:
	return err;
}

static void mxs_spi_release_hw(struct mxs_spi *ss)
{
	if (ss->clk && !IS_ERR(ss->clk)) {
		clk_disable(ss->clk);
		clk_put(ss->clk);
	}
}

static int mxs_spi_setup_transfer(struct spi_device *spi,
				  struct spi_transfer *t)
{
	u8 bits_per_word;
	u32 hz;
	struct mxs_spi *ss /* = spi_master_get_devdata(spi->master) */ ;
	u16 rate;

	ss = spi_master_get_devdata(spi->master);
	bits_per_word = spi->bits_per_word;
	if (t && t->bits_per_word)
		bits_per_word = t->bits_per_word;

	/*
	   Calculate speed:
	   - by default, use maximum speed from ssp clk
	   - if device overrides it, use it
	   - if transfer specifies other speed, use transfer's one
	 */
	hz = 1000 * ss->speed_khz / ss->divider;
	if (spi->max_speed_hz)
		hz = min(hz, spi->max_speed_hz);
	if (t && t->speed_hz)
		hz = min(hz, t->speed_hz);

	if (hz == 0) {
		dev_err(&spi->dev, "Cannot continue with zero clock\n");
		return -EINVAL;
	}

	if (bits_per_word != 8) {
		dev_err(&spi->dev, "%s, unsupported bits_per_word=%d\n",
			__func__, bits_per_word);
		return -EINVAL;
	}

	dev_dbg(&spi->dev, "Requested clk rate = %uHz, max = %ukHz/%d = %uHz\n",
		hz, ss->speed_khz, ss->divider,
		ss->speed_khz * 1000 / ss->divider);

	if (ss->speed_khz * 1000 / ss->divider < hz) {
		dev_err(&spi->dev, "%s, unsupported clock rate %uHz\n",
			__func__, hz);
		return -EINVAL;
	}

	rate = 1000 * ss->speed_khz / ss->divider / hz;

	__raw_writel(BF_SSP_TIMING_CLOCK_DIVIDE(ss->divider) |
		     BF_SSP_TIMING_CLOCK_RATE(rate - 1),
		     ss->regs + HW_SSP_TIMING);

	__raw_writel(BF_SSP_CTRL1_SSP_MODE(BV_SSP_CTRL1_SSP_MODE__SPI) |
		     BF_SSP_CTRL1_WORD_LENGTH
		     (BV_SSP_CTRL1_WORD_LENGTH__EIGHT_BITS) |
		     ((spi->mode & SPI_CPOL) ? BM_SSP_CTRL1_POLARITY : 0) |
		     ((spi->mode & SPI_CPHA) ? BM_SSP_CTRL1_PHASE : 0) |
		     (pio ? 0 : BM_SSP_CTRL1_DMA_ENABLE),
		     ss->regs + HW_SSP_CTRL1);

	__raw_writel(0x00, ss->regs + HW_SSP_CMD0_SET);

	return 0;
}

static void mxs_spi_cleanup(struct spi_device *spi)
{
	return;
}




/* the spi->mode bits understood by this driver: */
#define MODEBITS (SPI_CPOL | SPI_CPHA)
static int mxs_spi_setup(struct spi_device *spi)
{
	struct mxs_spi *ss;
	int err = 0;

	ss = spi_master_get_devdata(spi->master);

	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	if (spi->mode & ~MODEBITS) {
		dev_err(&spi->dev, "%s: unsupported mode bits %x\n",
			__func__, spi->mode & ~MODEBITS);
		err = -EINVAL;
		goto out;
	}

	dev_dbg(&spi->dev, "%s, mode %d, %u bits/w\n",
		__func__, spi->mode & MODEBITS, spi->bits_per_word);

	err = mxs_spi_setup_transfer(spi, NULL);
	if (err)
		goto out;
	return 0;

out:
	dev_err(&spi->dev, "Failed to setup transfer, error = %d\n", err);
	return err;
}

static inline u32 mxs_spi_cs(unsigned cs)
{
	return ((cs & 1) ? BM_SSP_CTRL0_WAIT_FOR_CMD : 0) |
	    ((cs & 2) ? BM_SSP_CTRL0_WAIT_FOR_IRQ : 0);
}

static int mxs_spi_txrx_dma(struct mxs_spi *ss, int cs,
			    unsigned char *buf, dma_addr_t dma_buf, int len,
			     int write) //int *first, int *last,
{
	u32 c0 = 0, xfer_size = 0;
	dma_addr_t spi_buf_dma = dma_buf;
	int count, status = 0;
	enum dma_data_direction dir = write ? DMA_TO_DEVICE : DMA_FROM_DEVICE;

	//c0 |= (*first ? BM_SSP_CTRL0_LOCK_CS : 0);
	//c0 |= (*last ? BM_SSP_CTRL0_IGNORE_CRC : 0);
	c0 |= (write ? 0 : BM_SSP_CTRL0_READ);
	c0 |= BM_SSP_CTRL0_DATA_XFER;

	c0 |= mxs_spi_cs(cs);

	if (ss->ver_major > 3) {
		xfer_size = BF_SSP_XFER_SIZE_XFER_COUNT(len);
		__raw_writel(xfer_size, ss->regs + HW_SSP_XFER_SIZE);
	} else {
		c0 |= BF_SSP_CTRL0_XFER_COUNT(len);
	}

	if (!dma_buf)
		spi_buf_dma = dma_map_single(ss->master_dev, buf, len, dir);

	ss->pdesc->cmd.cmd.bits.bytes = len;
	ss->pdesc->cmd.cmd.bits.pio_words = 1;
	ss->pdesc->cmd.cmd.bits.wait4end = 1;
	ss->pdesc->cmd.cmd.bits.dec_sem = 1;
	ss->pdesc->cmd.cmd.bits.irq = 1;
	ss->pdesc->cmd.cmd.bits.command = write ? DMA_READ : DMA_WRITE;
	ss->pdesc->cmd.address = spi_buf_dma;
	ss->pdesc->cmd.pio_words[0] = c0;
	mxs_dma_desc_append(ss->dma, ss->pdesc);

	mxs_dma_reset(ss->dma);
	mxs_dma_ack_irq(ss->dma);
	mxs_dma_enable_irq(ss->dma, 1);
	init_completion(&ss->done);
	mxs_dma_enable(ss->dma);
	wait_for_completion(&ss->done);
	count = 10000;
	while ((__raw_readl(ss->regs + HW_SSP_CTRL0) & BM_SSP_CTRL0_RUN)
	       && count--)
		continue;
	if (count <= 0) {
		printk(KERN_ERR "%c: timeout on line %s:%d\n",
		       write ? 'W' : 'C', __func__, __LINE__);
		status = -ETIMEDOUT;
	}

	if (!dma_buf)
		dma_unmap_single(ss->master_dev, spi_buf_dma, len, dir);

	return status;
}


static inline void mxs_spi_enable(u8 sel ,int ssp) //struct mxs_spi *ss 
{
	if( ssp >= 0xF0014000) {
		if( sel ) {
			gpio_set_value(GPIO_XCS,0); //0x00000714
		}
		else
		{
			gpio_set_value(GPIO_XDCS,0);
		}
	}
	else {
		if( sel ) {
			gpio_set_value(GPIO_XCS2,0); //0x00000714
		}
		else
		{
			gpio_set_value(GPIO_XDCS2,0);
		}
	}
//	__raw_writel(BM_SSP_CTRL0_LOCK_CS, ss->regs + HW_SSP_CTRL0_SET);
//	__raw_writel(BM_SSP_CTRL0_IGNORE_CRC, ss->regs + HW_SSP_CTRL0_CLR);
	
}

static inline void mxs_spi_disable( int ssp )  //struct mxs_spi *ss 
{
//	__raw_writel(BM_SSP_CTRL0_LOCK_CS, ss->regs + HW_SSP_CTRL0_CLR);
//	__raw_writel(BM_SSP_CTRL0_IGNORE_CRC, ss->regs + HW_SSP_CTRL0_SET);
	if( ssp  >= 0xF0014000){
		gpio_set_value(GPIO_XCS,1);
		gpio_set_value(GPIO_XDCS,1);
	}
	else{
		gpio_set_value(GPIO_XCS2,1);
		gpio_set_value(GPIO_XDCS2,1);
	}

}

static int mxs_spi_txrx_pio(struct mxs_spi *ss, int cs,
			    unsigned char *buf, int len,
			    int write)
{
	int count;

//	__raw_writel(mxs_spi_cs(cs), ss->regs + HW_SSP_CTRL0_SET);

	while (len--) {

		/* byte-by-byte */
		if (ss->ver_major > 3) {
			__raw_writel(1, ss->regs + HW_SSP_XFER_SIZE);
		} else {
			__raw_writel(BM_SSP_CTRL0_XFER_COUNT,
				     ss->regs + HW_SSP_CTRL0_CLR);
			__raw_writel(1, ss->regs + HW_SSP_CTRL0_SET);
		}

		if (write)
			__raw_writel(BM_SSP_CTRL0_READ,
				     ss->regs + HW_SSP_CTRL0_CLR);
		else
			__raw_writel(BM_SSP_CTRL0_READ,
				     ss->regs + HW_SSP_CTRL0_SET);

		/* Run! */
		__raw_writel(BM_SSP_CTRL0_RUN, ss->regs + HW_SSP_CTRL0_SET);
		count = 100000;
		while (((__raw_readl(ss->regs + HW_SSP_CTRL0) &
			 BM_SSP_CTRL0_RUN) == 0) && count--)
			continue;
		if (count <= 0) {
			printk(KERN_ERR "%c: timeout on line %s:%d\n",
			       write ? 'W' : 'C', __func__, __LINE__);
			break;
		}

		if (write)
			__raw_writel(*buf, ss->regs + HW_SSP_DATA);

		/* Set TRANSFER */
		__raw_writel(BM_SSP_CTRL0_DATA_XFER,
			     ss->regs + HW_SSP_CTRL0_SET);

		if (!write) {
			count = 100000;
			while (count-- &&
			       (__raw_readl(ss->regs + HW_SSP_STATUS) &
				BM_SSP_STATUS_FIFO_EMPTY))
				continue;
			if (count <= 0) {
				printk(KERN_ERR "%c: timeout on line %s:%d\n",
				       write ? 'W' : 'C', __func__, __LINE__);
				break;
			}
			*buf = (__raw_readl(ss->regs + HW_SSP_DATA) & 0xFF);
		}

		count = 100000;
		while ((__raw_readl(ss->regs + HW_SSP_CTRL0) & BM_SSP_CTRL0_RUN)
		       && count--)
			continue;
		if (count <= 0) {
			printk(KERN_ERR "%c: timeout on line %s:%d\n",
			       write ? 'W' : 'C', __func__, __LINE__);
			break;
		}

		/* advance to the next byte */
		buf++;
	}
	return len < 0 ? 0 : -ETIMEDOUT;
}

 extern  struct mxs_spi *g_pss;
 extern  int bQueWait ;
 extern  struct mutex	pcm_lock;

 extern  struct mxs_spi *g_pss2;
 extern  int bQueWait2 ;
 extern  struct mutex	pcm_lock2;
  
extern unsigned char * g_pcmData ;
extern  int g_pcmDataHead , g_pcmDataTail , g_pcmSize;

extern unsigned char * g_pcmData2 ;
extern  int g_pcmDataHead2 , g_pcmDataTail2 , g_pcmSize2;


#define PCM_BUFF (1024*16)
#define HDA0 0x08
#define HDA1 0x09
#define SPI_W 0x02
#define SPI_R 0x03


static int spi_thread(void *data)
{
	//int logPre10 = 10, logFirst = 30;
	int pre_autoReadPCM = 0, tempHead=0;
	unsigned short dataLen = 0,readLen = 0, i =0;
	unsigned char  txH1[2] = {0x03, 0x09}; //{SPI_R, HDA1};
	unsigned char  txH0[2] = {0x03, 0x08}; //{SPI_R, HDA0};
	

	if( g_pcmData != NULL  )
	{
	
		printk("lhg spi_thread start\n");
		while ( 1 ) {
		if( g_autoReadPCM )
		{
			if( !pre_autoReadPCM  )
			{
				g_pcmDataHead = 0;
				g_pcmDataTail = 0;
				g_pcmSize = 0;
			//	logPre10 = 10;
			//	logFirst = 30;
				pre_autoReadPCM = 1;
				//printk(KERN_ERR"reset pcm\n" );
			}
			readLen = dataLen = 0;
			
			DReqValue = gpio_get_value(GPIO_DREQ);
			
			if( !DReqValue) {
				bQueWait = 1;
				wait_event_timeout(g_pss->wait_que, DReqValue,msecs_to_jiffies(SPI_TIMEOUT));
			}

			spin_lock(&spiLock);
			mxs_spi_enable(1 , (u32)g_pss->regs);
			mxs_spi_txrx_pio(g_pss, 1, txH1, 2,  1);
			mxs_spi_txrx_pio(g_pss, 1, (unsigned char *)&dataLen, 2,  0);
			mxs_spi_disable( (u32)g_pss->regs );
			
			readLen = ( dataLen << 8 ) | (dataLen >> 8);

			
		/*	if(  (readLen > 1000) && (logFirst > 0) ){
				printk(KERN_ERR"O:%d %d\n",readLen ,g_pcmSize);
				logPre10 = 3;
				logFirst--;
			}*/
			
			
			readLen = readLen >= 256 ? 256 : 0;
			tempHead = g_pcmDataHead;
			for( i = 0; i < readLen; i++)
			{
				/*DReqValue = gpio_get_value(GPIO_DREQ);
			
				if( !DReqValue) {
					bQueWait = 1;
					wait_event_timeout(g_pss->wait_que, DReqValue,msecs_to_jiffies(SPI_TIMEOUT));
				}*/
				mxs_spi_enable(1 , (u32)g_pss->regs);
				mxs_spi_txrx_pio(g_pss, 1, txH0, 2, 1) ;
				mxs_spi_txrx_pio(g_pss, 1, g_pcmData + tempHead , 2, 0) ;
				mxs_spi_disable( (u32)g_pss->regs );
				tempHead = (tempHead + 2 ) % PCM_BUFF ;
				
			}
			spin_unlock(&spiLock);

			mutex_lock(&pcm_lock);
			g_pcmDataHead  = tempHead;
			//g_pcmSize = (g_pcmDataHead >= g_pcmDataTail) ? (g_pcmDataHead - g_pcmDataTail) : ( g_pcmDataHead + PCM_BUFF - g_pcmDataTail );
			g_pcmSize += readLen*2;

			mutex_unlock(&pcm_lock);
			if( (jiffies - g_readjiffies) / HZ > 8 ){
				g_autoReadPCM = 0;
				g_readjiffies = jiffies;
			}
			msleep(3);
		}
		else{
			pre_autoReadPCM = g_autoReadPCM ;
			msleep(5);
		}
		
		
		}
	}
	

	return 0;
}


static int spi2_thread(void *data)
{
	//int logPre10 = 10, logFirst = 30;
	int pre_autoReadPCM = 0, tempHead=0;
	unsigned short dataLen = 0,readLen = 0, i =0;
	unsigned char  txH1[2] = {0x03, 0x09}; //{SPI_R, HDA1};
	unsigned char  txH0[2] = {0x03, 0x08}; //{SPI_R, HDA0};
	

	if( g_pcmData2 != NULL  )
	{
	
		printk("lhg spi2_thread start\n");
		while ( 1 ) {
		if( g_autoReadPCM2 )
		{
			if( !pre_autoReadPCM  )
			{
				g_pcmDataHead2 = 0;
				g_pcmDataTail2 = 0;
				g_pcmSize2 = 0;
				pre_autoReadPCM = 1;
			}
			readLen = dataLen = 0;
			
			DReqValue2 = gpio_get_value(GPIO_DREQ2);
			
			if( !DReqValue2) {
				bQueWait2 = 1;
				wait_event_timeout(g_pss2->wait_que, DReqValue2,msecs_to_jiffies(SPI_TIMEOUT));
			}

			spin_lock(&spiLock);
			mxs_spi_enable(1,(u32)g_pss2->regs);
			mxs_spi_txrx_pio(g_pss2, 1, txH1, 2,  1);
			mxs_spi_txrx_pio(g_pss2, 1, (unsigned char *)&dataLen, 2,  0);
			mxs_spi_disable((u32)g_pss2->regs );
			
			readLen = ( dataLen << 8 ) | (dataLen >> 8);

			readLen = readLen >= 256 ? 256 : 0;
			tempHead = g_pcmDataHead2;
			for( i = 0; i < readLen; i++)
			{
				mxs_spi_enable(1,(u32)g_pss2->regs);
				mxs_spi_txrx_pio(g_pss2, 1, txH0, 2, 1) ;
				mxs_spi_txrx_pio(g_pss2, 1, g_pcmData2 + tempHead , 2, 0) ;
				mxs_spi_disable( (u32)g_pss2->regs );
				tempHead = (tempHead + 2 ) % PCM_BUFF ;
				
			}
			spin_unlock(&spiLock);

			mutex_lock(&pcm_lock2);
			g_pcmDataHead2  = tempHead;
			//g_pcmSize = (g_pcmDataHead >= g_pcmDataTail) ? (g_pcmDataHead - g_pcmDataTail) : ( g_pcmDataHead + PCM_BUFF - g_pcmDataTail );
			g_pcmSize2 += readLen*2;

			mutex_unlock(&pcm_lock2);
			if( (jiffies - g_readjiffies2) / HZ > 8 ){
				g_autoReadPCM2 = 0;
				g_readjiffies2 = jiffies;
			}
			msleep(3);
		}
		else{
			pre_autoReadPCM = g_autoReadPCM2 ;
			msleep(5);
		}
		
		
		}
	}
	

	return 0;
}


static int mxs_spi_handle_message(struct mxs_spi *ss, struct spi_message *m)
{
	struct spi_transfer *t, *tmp_t;
	u8 chip_sel_d = 0xff,chip_sel_d2 = 0xff;
	int status = 0;
	int cs = 0;
	//cs = m->spi->chip_select;

	list_for_each_entry_safe(t, tmp_t, &m->transfers, transfer_list) {

		if (t->rx_buf && t->tx_buf) {
			pr_debug("%s: cannot send and receive simultaneously\n",
				 __func__);
			return -EINVAL;
		}
		
		if( (u32)ss->regs >= 0xF0014000)
		{
			chip_sel = cs = t->chip_select;
			DReqValue = gpio_get_value(GPIO_DREQ);
			
			if( !DReqValue) {
				bQueWait = 1;
				wait_event_timeout(ss->wait_que, DReqValue,msecs_to_jiffies(SPI_TIMEOUT));
			}
			spin_lock(&spiLock);
			
			if( chip_sel_d != chip_sel){
				chip_sel_d = chip_sel;
				mxs_spi_disable( (u32)ss->regs );
			}
			mxs_spi_enable(chip_sel,(u32)ss->regs);
		}
		else {
			chip_sel2 = cs = t->chip_select;
			DReqValue2 = gpio_get_value(GPIO_DREQ2);
			
			if( !DReqValue2) {
				bQueWait2 = 1;
				wait_event_timeout(ss->wait_que, DReqValue2,msecs_to_jiffies(SPI_TIMEOUT));
			}
			spin_lock(&spiLock);

			if( chip_sel_d2 != chip_sel2){
				chip_sel_d2 = chip_sel2;
				mxs_spi_disable( (u32)ss->regs );
			}
			mxs_spi_enable(chip_sel2,(u32)ss->regs);

		}

		
		mxs_spi_setup_transfer(m->spi, t);

		/*
		   REVISIT:
		   here driver completely ignores setting of t->cs_change
		 */
		if (t->tx_buf) {
			status = pio ?
			    mxs_spi_txrx_pio(ss, cs, (void *)t->tx_buf,
					     t->len,  1) :
			    mxs_spi_txrx_dma(ss, cs, (void *)t->tx_buf,
					     t->tx_dma, t->len,  1);
			if (debug) {
				if (t->len < 0x10)
					print_hex_dump_bytes("Tx ",
							     DUMP_PREFIX_OFFSET,
							     t->tx_buf, t->len);
				else
					pr_debug("Tx: %d bytes\n", t->len);
			}
		}
		if (t->rx_buf) {
			status = pio ?
			    mxs_spi_txrx_pio(ss, cs, t->rx_buf,
					     t->len, 0) :
			    mxs_spi_txrx_dma(ss, cs, t->rx_buf,
					     t->rx_dma, t->len, 0);
			if (debug) {
				if (t->len < 0x10)
					print_hex_dump_bytes("Rx ",
							     DUMP_PREFIX_OFFSET,
							     t->rx_buf, t->len);
				else
					pr_debug("Rx: %d bytes\n", t->len);
			}
		}
		
		mxs_spi_disable( (u32)ss->regs );
		spin_unlock(&spiLock);
		
		m->actual_length += t->len;
		udelay(t->delay_usecs);
		if (status)
			break;

	}
	return status;
}


/**
 * mxs_spi_handle
 *
 * The workhorse of the driver - it handles messages from the list
 *
 **/
static void mxs_spi_handle(struct work_struct *w)
{
	struct mxs_spi *ss = container_of(w, struct mxs_spi, work);
	unsigned long flags;
	struct spi_message *m;

	if( (u32)ss->regs >= 0xF0014000)
		g_pss = ss;
	else
		g_pss2 = ss;


	BUG_ON(w == NULL);

	spin_lock_irqsave(&ss->lock, flags);
	while (!list_empty(&ss->queue) ) {  
		m = list_entry(ss->queue.next, struct spi_message, queue);
		
		list_del_init(&m->queue);
		spin_unlock_irqrestore(&ss->lock, flags);

		m->status = mxs_spi_handle_message(ss, m);
		if (m->complete){
			m->complete(m->context);
			mxs_spi_disable( (u32)ss->regs );
		}
	//	else
	//		printk(KERN_DEBUG "lhg spi no recv complete\n");

		spin_lock_irqsave(&ss->lock, flags);
	}
	spin_unlock_irqrestore(&ss->lock, flags);

	return;
}

/**
 * mxs_spi_transfer
 *
 * Called indirectly from spi_async, queues all the messages to
 * spi_handle_message
 *
 * @spi: spi device
 * @m: message to be queued
**/
static int mxs_spi_transfer(struct spi_device *spi, struct spi_message *m)
{
	struct mxs_spi *ss = spi_master_get_devdata(spi->master);
	unsigned long flags;

	m->actual_length = 0;
	m->status = -EINPROGRESS;
	spin_lock_irqsave(&ss->lock, flags);
	list_add_tail(&m->queue, &ss->queue);
	queue_work(ss->workqueue, &ss->work);
	spin_unlock_irqrestore(&ss->lock, flags);
	return 0;
}

static irqreturn_t mxs_spi_irq_dma(int irq, void *dev_id)
{
	struct mxs_spi *ss = dev_id;
	mxs_dma_ack_irq(ss->dma);
	mxs_dma_cooked(ss->dma, NULL);
	complete(&ss->done);
	return IRQ_HANDLED;
}

static irqreturn_t mxs_spi_irq_err(int irq, void *dev_id)
{
	struct mxs_spi *ss = dev_id;
	u32 c1, st;

	c1 = __raw_readl(ss->regs + HW_SSP_CTRL1);
	st = __raw_readl(ss->regs + HW_SSP_STATUS);
	printk(KERN_ERR "IRQ - ERROR!, status = 0x%08X, c1 = 0x%08X\n", st, c1);
	__raw_writel(c1 & 0xCCCC0000, ss->regs + HW_SSP_CTRL1_CLR);

	return IRQ_HANDLED;
}

static int __init mxs_spi_probe(struct platform_device *dev)
{
	struct mxs_spi_platform_data *pdata;
	int err = 0;
	struct spi_master *master;
	struct mxs_spi *ss;
	struct resource *r;
	u32 mem;


	pdata = dev->dev.platform_data;
	if (pdata && pdata->hw_pin_init) {
		err = pdata->hw_pin_init();
		if (err)
			goto out0;
	}

	//dump_stack();
	/* Get resources(memory, IRQ) associated with the device */
	master = spi_alloc_master(&dev->dev, sizeof(struct mxs_spi));

	if (master == NULL) {
		err = -ENOMEM;
		goto out0;
	}

	platform_set_drvdata(dev, master);

	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		err = -ENODEV;
		goto out_put_master;
	}

	ss = spi_master_get_devdata(master);
	ss->master_dev = &dev->dev;

	INIT_WORK(&ss->work, mxs_spi_handle);
	INIT_LIST_HEAD(&ss->queue);
	spin_lock_init(&ss->lock);
	init_waitqueue_head(&ss->wait_que);
	
	ss->workqueue = create_singlethread_workqueue(dev_name(&dev->dev));
	master->transfer = mxs_spi_transfer;
	master->setup = mxs_spi_setup;
	master->cleanup = mxs_spi_cleanup;

	if (!request_mem_region(r->start,
				resource_size(r), dev_name(&dev->dev))) {
		err = -ENXIO;
		goto out_put_master;
	}
	mem = r->start;

	ss->regs = IO_ADDRESS(r->start);
	if( (u32)ss->regs >= 0xF0014000 )
	{
		ss->irq_dma = platform_get_irq(dev, 0);
		if (ss->irq_dma < 0) {
			err = -ENXIO;
			goto out_put_master;
		}
		ss->irq_err = platform_get_irq(dev, 1);
		if (ss->irq_err < 0) {
			err = -ENXIO;
			goto out_put_master;
		}

		r = platform_get_resource(dev, IORESOURCE_DMA, 0);
		if (r == NULL) {
			err = -ENODEV;
			goto out_put_master;
		}

		ss->dma = r->start;
		err = mxs_dma_request(ss->dma, &dev->dev, (char *)dev_name(&dev->dev));
		if (err)
			goto out_put_master;

		ss->pdesc = mxs_dma_alloc_desc();
		if (ss->pdesc == NULL || IS_ERR(ss->pdesc)) {
			err = -ENOMEM;
			goto out_free_dma;
		}
		
	}
		master->bus_num = dev->id + 1;
		master->num_chipselect = 1;  
		master->mode_bits = SPI_CPHA | SPI_CPOL;// SPI_MODE_MASK;

		/* SPI controller initializations */
		err = mxs_spi_init_hw(ss);
		if (err) {
			dev_dbg(&dev->dev, "cannot initialize hardware\n");
			goto out_free_dma_desc;
		}

		clk_set_rate(ss->clk, 120 * 1000 * 1000);
		ss->speed_khz = clk_get_rate(ss->clk) / 1000;
		ss->divider = 2;
		dev_info(&dev->dev, "Max possible speed %d = %ld/%d kHz\n",
			 ss->speed_khz, clk_get_rate(ss->clk), ss->divider);

		ss->ver_major = __raw_readl(ss->regs + HW_SSP_VERSION) >> 24;
		
	if( (u32)ss->regs >= 0xF0014000 )
	{
		/* Register for SPI Interrupt */
		err = request_irq(ss->irq_dma, mxs_spi_irq_dma, 0,
				  dev_name(&dev->dev), ss);
		if (err) {
			dev_dbg(&dev->dev, "request_irq failed, %d\n", err);
			goto out_release_hw;
		}
		err = request_irq(ss->irq_err, mxs_spi_irq_err, IRQF_SHARED,
				  dev_name(&dev->dev), ss);
		if (err) {
			dev_dbg(&dev->dev, "request_irq(error) failed, %d\n", err);
			goto out_free_irq;
		}

	}

	err = spi_register_master(master);
	if (err) {
		dev_dbg(&dev->dev, "cannot register spi master, %d\n", err);
		goto out_free_irq_2;
	}
	dev_info(&dev->dev, "at 0x%08X mapped to 0x%08X, irq=%d, bus %d, %s ver_major %d\n",
		 mem, (u32) ss->regs, ss->irq_dma,
		 master->bus_num, pio ? "PIO" : "DMA", ss->ver_major);
	return 0;

out_free_irq_2:
	free_irq(ss->irq_err, ss);
out_free_irq:
	free_irq(ss->irq_dma, ss);
out_free_dma_desc:
	mxs_dma_free_desc(ss->pdesc);
out_free_dma:
	mxs_dma_release(ss->dma, &dev->dev);
out_release_hw:
	mxs_spi_release_hw(ss);
out_put_master:
	spi_master_put(master);
out0:
	return err;
}

static int __devexit mxs_spi_remove(struct platform_device *dev)
{
	struct mxs_spi *ss;
	struct spi_master *master;
	struct mxs_spi_platform_data *pdata = dev->dev.platform_data;

	if (pdata && pdata->hw_pin_release)
		pdata->hw_pin_release();

	master = platform_get_drvdata(dev);
	if (master == NULL)
		goto out0;
	ss = spi_master_get_devdata(master);
	if (ss == NULL)
		goto out1;
	free_irq(ss->irq_err, ss);
	free_irq(ss->irq_dma, ss);
	if (ss->workqueue)
		destroy_workqueue(ss->workqueue);
	mxs_dma_free_desc(ss->pdesc);
	mxs_dma_release(ss->dma, &dev->dev);
	mxs_spi_release_hw(ss);
	platform_set_drvdata(dev, 0);
out1:
	spi_master_put(master);
out0:
	return 0;
}

#ifdef CONFIG_PM
static int mxs_spi_suspend(struct platform_device *pdev, pm_message_t pmsg)
{
	struct mxs_spi *ss;
	struct spi_master *master;

	master = platform_get_drvdata(pdev);
	ss = spi_master_get_devdata(master);

	ss->saved_timings = __raw_readl(ss->regs + HW_SSP_TIMING);
	clk_disable(ss->clk);

	return 0;
}

static int mxs_spi_resume(struct platform_device *pdev)
{
	struct mxs_spi *ss;
	struct spi_master *master;

	master = platform_get_drvdata(pdev);
	ss = spi_master_get_devdata(master);

	clk_enable(ss->clk);
	__raw_writel(BM_SSP_CTRL0_SFTRST | BM_SSP_CTRL0_CLKGATE,
		     ss->regs + HW_SSP_CTRL0_CLR);
	__raw_writel(ss->saved_timings, ss->regs + HW_SSP_TIMING);

	return 0;
}

#else
#define mxs_spi_suspend NULL
#define mxs_spi_resume  NULL
#endif

static struct platform_driver mxs_spi_driver = {
	.probe = mxs_spi_probe,
	.remove = __devexit_p(mxs_spi_remove),
	.driver = {
		   .name = "mxs-spi",
		   .owner = THIS_MODULE,
		   },
	.suspend = mxs_spi_suspend,
	.resume = mxs_spi_resume,
};

static int __init mxs_spi_init(void)
{
	spin_lock_init(&spiLock);
	g_pcmData = kmalloc(PCM_BUFF, GFP_KERNEL );
	printk("lhg g_pcmData:%x\n" ,g_pcmData);
	if( spi_kthread == NULL)
	 	spi_kthread = kthread_run(spi_thread, NULL, "spipcm");

	g_pcmData2 = kmalloc(PCM_BUFF, GFP_KERNEL );
	printk("lhg g_pcmData2:%x\n" ,g_pcmData2);
	if( spi_kthread2 == NULL)
	 	spi_kthread2 = kthread_run(spi2_thread, NULL, "spi2pcm");

	return platform_driver_register(&mxs_spi_driver);
}

static void __exit mxs_spi_exit(void)
{
	platform_driver_unregister(&mxs_spi_driver);
}

module_init(mxs_spi_init);
module_exit(mxs_spi_exit);
module_param(pio, int, S_IRUGO);
module_param(debug, int, S_IRUGO);
MODULE_AUTHOR("dmitry pervushin <dimka@embeddedalley.com>");
MODULE_DESCRIPTION("MXS SPI/SSP");
MODULE_LICENSE("GPL");
