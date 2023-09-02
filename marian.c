// SPDX-License-Identifier: GPL-2.0-only
/*
 *   ALSA driver for MARIAN Seraph audio interfaces
 *
 *   Copyright (c) 2012 Florian Faber <faberman@linuxproaudio.org>,
 *		   2023 Ivan Orlov <ivan.orlov0322@gmail.com>
 */

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/info.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#define SERAPH_RD_IRQ_STATUS      0x00
#define SERAPH_RD_HWPOINTER       0x8C

#define SERAPH_WR_DMA_ADR         0x04
#define SERAPH_WR_ENABLE_CAPTURE  0x08
#define SERAPH_WR_ENABLE_PLAYBACK 0x0C
#define SERAPH_WR_DMA_BLOCKS      0x10

#define SERAPH_WR_DMA_ENABLE      0x84
#define SERAPH_WR_IE_ENABLE       0xAC

#define PCI_VENDOR_ID_MARIAN            0x1382
#define PCI_DEVICE_ID_MARIAN_SERAPH_M2  0x5021

#define RATE_SLOW	54000
#define RATE_NORMAL	108000

#define SPEEDMODE_SLOW	 1
#define SPEEDMODE_NORMAL 2
#define SPEEDMODE_FAST	 4

#define MARIAN_PORTS_TYPE_INPUT	 0
#define MARIAN_PORTS_TYPE_OUTPUT 1

#define ERR_DEAD_WRITE	BIT(0)
#define ERR_DEAD_READ	BIT(1)
#define ERR_DATA_LOST	BIT(2)
#define ERR_PAGE_CONF	BIT(3)
#define ERR_INT_PLAY	BIT(10)
#define ERR_INT_REC	BIT(13)

#define STATUS_ST_READY		BIT(4)
#define STATUS_INT_PLAY		BIT(8)
#define STATUS_INT_PPLAY	BIT(9)
#define STATUS_INT_REC		BIT(11)
#define STATUS_INT_PREC		BIT(12)
#define STATUS_INT_PREP		BIT(14)
#define WCLOCK_NEW_VAL		BIT(31)
#define SPI_ALL_READY		BIT(31)

#define M2_CLOCK_SRC_CNT	4
#define M2_CLOCK_SRC_DCO	1
#define M2_CLOCK_SRC_SYNCBUS	2
#define M2_CLOCK_SRC_MADI1	4
#define M2_CLOCK_SRC_MADI2	5

#define M2_SYNC_STATE_CNT	3
#define M2_CHNL_MODE_CNT	2
#define M2_FRAME_MODE_CNT	2

#define M2_INP1_SYNC_CTL_ID	0
#define M2_INP1_CM_CTL_ID	0
#define M2_INP1_FM_CTL_ID	0
#define M2_INP1_FREQ_CTL_ID	4
#define M2_OUT1_CM_CTL_ID	0
#define M2_OUT1_FM_CTL_ID	0
#define M2_INP2_SYNC_CTL_ID	1
#define M2_INP2_CM_CTL_ID	1
#define M2_INP2_FM_CTL_ID	1
#define M2_INP2_FREQ_CTL_ID	5
#define M2_OUT2_CM_CTL_ID	1
#define M2_OUT2_FM_CTL_ID	1

// MADI FPGA register 0x40
// Use internal (=0) or external PLL (=1)
#define M2_PLL         2

// MADI FPGA register 0x41
// Enable both MADI transmitters (=1)
#define M2_TX_ENABLE   0
// Use int (=0) or 32bit IEEE float (=1)
#define M2_INT_FLOAT   4
// Big endian (=0), little endian (=1)
#define M2_ENDIANNESS  5
// MSB first (=0), LSB first (=1)
#define M2_BIT_ORDER   6

// MADI FPGA register 0x42
// Send 56ch (=0) or 64ch (=1) MADI frames
#define M2_PORT1_MODE  0
// Send 48kHz (=0) or 96kHz (=1) MADI frames
#define M2_PORT1_FRAME 1
// Send 56ch (=0) or 64ch (=1) MADI frames
#define M2_PORT2_MODE  2
// Send 48kHz (=0) or 96kHz (=1) MADI frames
#define M2_PORT2_FRAME 3

struct marian_card_descriptor;
struct marian_card;

struct marian_card_descriptor {
	char *name;
	char *port_names;
	unsigned int speedmode_max;
	unsigned int ch_in;
	unsigned int ch_out;
	unsigned int dma_ch_offset;
	unsigned int midi_in;
	unsigned int midi_out;
	unsigned int serial_in;
	unsigned int serial_out;
	unsigned int wck_in;
	unsigned int wck_out;

	unsigned int dma_bufsize;

	void (*hw_constraints_func)(struct marian_card *marian,
				    struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params);
	/* custom function to set up ALSA controls */
	void (*create_controls)(struct marian_card *marian);
	/* init is called after probing the card */
	int (*init_card)(struct marian_card *marian);
	void (*free_card)(struct marian_card *marian);
	/* prepare is called when ALSA is opening the card */
	void (*prepare)(struct marian_card *marian);
	void (*init_codec)(struct marian_card *marian);
	void (*set_speedmode)(struct marian_card *marian, unsigned int speedmode);
	void (*proc_status)(struct marian_card *marian, struct snd_info_buffer *buffer);
	void (*proc_ports)(struct marian_card *marian, struct snd_info_buffer *buffer,
			   unsigned int type);

	struct snd_pcm_hardware info_playback;
	struct snd_pcm_hardware info_capture;
};

struct marian_card {
	struct marian_card_descriptor *desc;

	struct snd_pcm_substream *playback_substream;
	struct snd_pcm_substream *capture_substream;

	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_dma_buffer dmabuf;

	struct pci_dev *pci;
	unsigned long port;
	void __iomem *iobase;
	int irq;

	unsigned int idx;
	/* card lock */
	spinlock_t lock;

	unsigned int stream_open;
	unsigned int period_size;

	/* speed mode: 1, 2, 4 times FS */
	unsigned int speedmode;

	/* 0..15, meaning depending on the card type */
	unsigned int clock_source;

	/* Frequency of the internal oscillator (Hertz) */
	unsigned int dco;
	/* [0..1) part of the internal oscillator frequency (milli Hertz) */
	unsigned int dco_millis;

	/* [-200 .. 200] Two semitone 'musical' adjustment */
	int detune;

	/* WCK input termination on (1)/off (0) */
	unsigned int wck_term;

	/* WCK output source */
	unsigned int wck_output;

	void *card_specific;
};

enum CLOCK_SOURCE {
	CLOCK_SRC_INTERNAL = 0,
	CLOCK_SRC_SYNCBUS  = 1,
	CLOCK_SRC_INP1     = 2,
	CLOCK_SRC_INP2	   = 3,
	CLOCK_SRC_INP3	   = 4,
};

enum m2_num_mode {
	M2_NUM_MODE_INT		= 0,
	M2_NUM_MODE_FLOAT	= 1,
};

enum m2_endianness_mode {
	M2_BE	= 0,
	M2_LE	= 1,
};

struct m2_specific {
	u8 shadow_40;
	u8 shadow_41;
	u8 shadow_42;
	u8 frame;
};

static const struct pci_device_id snd_marian_ids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_MARIAN, PCI_DEVICE_ID_MARIAN_SERAPH_M2), 0, 0, 6},
	{ }
};

MODULE_DEVICE_TABLE(pci, snd_marian_ids);

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX; // Index 0-MAX
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR; // ID for this card

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for MARIAN PCI soundcard");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for MARIAN PCI soundcard");

static void snd_marian_card_free(struct snd_card *card)
{
	struct marian_card *marian = card->private_data;

	if (!marian)
		return;

	snd_dma_free_pages(&marian->dmabuf);

	if (marian->irq >= 0)
		free_irq(marian->irq, (void *)marian);

	if (marian->iobase)
		pci_iounmap(marian->pci, marian->iobase);

	if (marian->port)
		pci_release_regions(marian->pci);

	pci_disable_device(marian->pci);
}

static void marian_proc_status_generic(struct marian_card *marian, struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer, "*** Card registers\n");
	snd_iprintf(buffer, "RD 0x064: %08x (SPI bits written)\n", readl(marian->iobase + 0x64));
	snd_iprintf(buffer, "RD 0x068: %08x (SPI bits read)\n", readl(marian->iobase + 0x68));
	snd_iprintf(buffer, "RD 0x070: %08x (SPI bits status)\n", readl(marian->iobase + 0x70));
	snd_iprintf(buffer, "RD 0x088: %08x (Super clock measurement)\n",
		    readl(marian->iobase + 0x88));
	snd_iprintf(buffer, "RD 0x08C: %08x (HW Pointer)\n",
		    readl(marian->iobase + SERAPH_RD_HWPOINTER));
	snd_iprintf(buffer, "RD 0x094: %08x (Word clock measurement)\n",
		    readl(marian->iobase + 0x88));
	snd_iprintf(buffer, "RD 0x0F8: %08x (Extension board)\n",
		    readl(marian->iobase + 0xF8));
	snd_iprintf(buffer, "RD 0x244: %08x (DMA debug)\n",
		    readl(marian->iobase + 0x244));

	snd_iprintf(buffer, "\n*** Card status\n");
	snd_iprintf(buffer, "Firmware build: %08x\n", readl(marian->iobase + 0xFC));
	snd_iprintf(buffer, "Speed mode   : %uFS (1..%u)\n",
		    marian->speedmode, marian->desc->speedmode_max);
	snd_iprintf(buffer, "Clock master : %s\n", (marian->clock_source == 1) ? "yes" : "no");
	snd_iprintf(buffer, "DCO frequency: %d.%d Hz\n", marian->dco, marian->dco_millis);
	snd_iprintf(buffer, "DCO detune   : %d Cent\n", marian->detune);
}

static void snd_marian_proc_status(struct snd_info_entry  *entry, struct snd_info_buffer *buffer)
{
	struct marian_card *marian = entry->private_data;

	if (marian->desc->proc_status)
		marian->desc->proc_status(marian, buffer);
	else
		marian_proc_status_generic(marian, buffer);
}

/*
 * Default port name function, outputs the static string
 * port_names of the card descriptor regardless of current
 * speed mode and whether input or output ports are requested.
 */
static void marian_proc_ports_generic(struct marian_card *marian, struct snd_info_buffer *buffer,
			       unsigned int type)
{
	snd_iprintf(buffer, marian->desc->port_names);
}

static void snd_marian_proc_ports_in(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct marian_card *marian = entry->private_data;

	snd_iprintf(buffer, "# generated by MARIAN Seraph driver\n");
	if (marian->desc->proc_ports)
		marian->desc->proc_ports(marian, buffer, MARIAN_PORTS_TYPE_INPUT);
	else
		marian_proc_ports_generic(marian, buffer, MARIAN_PORTS_TYPE_INPUT);
}

static void snd_marian_proc_ports_out(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct marian_card *marian = entry->private_data;

	snd_iprintf(buffer, "# generated by MARIAN Seraph driver\n");
	if (marian->desc->proc_ports)
		marian->desc->proc_ports(marian, buffer, MARIAN_PORTS_TYPE_OUTPUT);
	else
		marian_proc_ports_generic(marian, buffer, MARIAN_PORTS_TYPE_OUTPUT);
}

static irqreturn_t snd_marian_interrupt(int irq, void *dev_id)
{
	struct marian_card *marian = (struct marian_card *)dev_id;
	unsigned int irq_status;

	irq_status = readl(marian->iobase + SERAPH_RD_IRQ_STATUS);

	if (irq_status & 0x00004800) {
		if (marian->playback_substream)
			snd_pcm_period_elapsed(marian->playback_substream);

		if (marian->capture_substream)
			snd_pcm_period_elapsed(marian->capture_substream);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int snd_marian_playback_open(struct snd_pcm_substream *substream)
{
	struct marian_card *marian = substream->private_data;

	substream->runtime->hw = marian->desc->info_playback;

	marian->playback_substream = substream;

	snd_pcm_set_sync(substream);

	return 0;
}

static int snd_marian_capture_open(struct snd_pcm_substream *substream)
{
	struct marian_card *marian = substream->private_data;

	substream->runtime->hw = marian->desc->info_capture;

	marian->capture_substream = substream;

	snd_pcm_set_sync(substream);

	return 0;
}

static int snd_marian_capture_release(struct snd_pcm_substream *substream)
{
	struct marian_card *marian = snd_pcm_substream_chip(substream);

	marian->capture_substream = NULL;

	return 0;
}

static int snd_marian_playback_release(struct snd_pcm_substream *substream)
{
	struct marian_card *marian = snd_pcm_substream_chip(substream);

	marian->playback_substream = NULL;

	return 0;
}

static void marian_generic_set_dco(struct marian_card *marian, unsigned int freq, unsigned int millis)
{
	u64 val, v2;
	s64 detune;

	dev_dbg(marian->card->dev, "(.., %u, %u)\n", freq, millis);

	val = (freq * 1000 + millis) * marian->speedmode;
	val <<= 36;

	if (marian->detune != 0) {
		/*
		 * DCO detune active
		 * this calculation takes a bit of a shortcut
		 * - should be implemented using a logarithmic scale
		 */
		detune = marian->detune * 100;
		v2 = val;
		div_u64(v2, 138564);
		detune *= v2;
		val += detune;
	}

	val = div_u64(val, 80000000);
	val = div_u64(val, 1000);

	dev_dbg(marian->card->dev, "  -> 0x%016llx (%llu)\n", val, val);
	writel((u32)val, marian->iobase + 0x88);

	marian->dco = freq;
	marian->dco_millis = millis;
}

static void marian_generic_set_speedmode(struct marian_card *marian, unsigned int speedmode)
{
	if (speedmode > marian->desc->speedmode_max)
		return;

	switch (speedmode) {
	case SPEEDMODE_SLOW:
		writel(0x03, marian->iobase + 0x80);
		writel(0x00, marian->iobase + 0x8C); // for 48kHz in 1FS mode
		marian->speedmode = SPEEDMODE_SLOW;
		break;
	case SPEEDMODE_NORMAL:
		writel(0x03, marian->iobase + 0x80);
		writel(0x01, marian->iobase + 0x8C); // for 96kHz in 2FS mode
		marian->speedmode = SPEEDMODE_NORMAL;
		break;
	case SPEEDMODE_FAST:
		writel(0x03, marian->iobase + 0x80);
		writel(0x00, marian->iobase + 0x8C); // for 192kHz in 4FS mode
		marian->speedmode = SPEEDMODE_FAST;
		break;
	}

	marian_generic_set_dco(marian, marian->dco, marian->dco_millis);
}

static int snd_marian_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct marian_card *marian = snd_pcm_substream_chip(substream);
	unsigned int speedmode;
	int size;

	dev_dbg(marian->card->dev,
		"%d ch %s @ %dHz, buffer size %d\n",
		params_channels(params),
		(substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? "playback" : "capture",
		params_rate(params),
		params_buffer_size(params));

	marian->period_size = params_buffer_size(params);
	dev_dbg(marian->card->dev,
		"period size: %d\n", marian->period_size);

	size = params_buffer_size(params) * params_channels(params) * 4;
	dev_dbg(marian->card->dev,
		"period buf size: %d\n", size);

	if (params_rate(params) < RATE_SLOW)
		speedmode = SPEEDMODE_SLOW;
	else if (params_rate(params) < RATE_NORMAL)
		speedmode = SPEEDMODE_NORMAL;
	else
		speedmode = SPEEDMODE_FAST;

	if (speedmode > marian->desc->speedmode_max) {
		dev_err(marian->card->dev,
			"Requested rate (%u Hz) higher than card's maximum\n",
			params_rate(params));
		_snd_pcm_hw_param_setempty(params, SNDRV_PCM_HW_PARAM_RATE);
		return -EBUSY;
	}

	if (marian->desc->set_speedmode)
		marian->desc->set_speedmode(marian, speedmode);
	else
		marian_generic_set_speedmode(marian, speedmode);

	marian->detune = 0;
	marian_generic_set_dco(marian, params_rate(params), 0);

	snd_pcm_set_runtime_buffer(substream, &marian->dmabuf);

	dev_dbg(marian->card->dev, "  stream    : %s\n",
		(substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? "playback" : "capture");
	dev_dbg(marian->card->dev,
		"  dma_area  : 0x%lx\n", (unsigned long)substream->runtime->dma_area);
	dev_dbg(marian->card->dev,
		"  dma_addr  : 0x%lx\n", (unsigned long)substream->runtime->dma_addr);
	dev_dbg(marian->card->dev, "  dma_bytes : 0x%lx (%d)\n",
		(unsigned long)substream->runtime->dma_bytes,
		(int)substream->runtime->dma_bytes);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		dev_dbg(marian->card->dev, "  setting card's DMA ADR to %08x\n",
			(unsigned int)marian->capture_substream->runtime->dma_addr);
		// We really want the dma_addr to be in the 32 bits. DMA mask needs to be set.
		writel((u32)marian->capture_substream->runtime->dma_addr,
		       marian->iobase + SERAPH_WR_DMA_ADR); // TODO Set DMA mask
	} else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dev_dbg(marian->card->dev, "  setting card's DMA ADR to %pad\n",
			&marian->playback_substream->runtime->dma_addr);
		// Same here: we need to set the DMA mask. Only then the conversion will be right
		writel((u32)marian->playback_substream->runtime->dma_addr,
		       marian->iobase + SERAPH_WR_DMA_ADR);
	}
	dev_dbg(marian->card->dev,
		"  setting card's DMA block count to %d\n", marian->period_size / 16);
	writel(marian->period_size / 16, marian->iobase + SERAPH_WR_DMA_BLOCKS);

	// apply optional card specific hw constraints
	if (marian->desc->hw_constraints_func)
		marian->desc->hw_constraints_func(marian, substream, params);

	return 0;
}

static int snd_marian_prepare(struct snd_pcm_substream *substream)
{
	struct marian_card *marian = snd_pcm_substream_chip(substream);

	dev_dbg(marian->card->dev, "  stream    : %s\n",
		(substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? "playback" : "capture");
	// TODO Wrong conversions
	dev_dbg(marian->card->dev, "  dma_area  : 0x%lx\n",
		(unsigned long)substream->runtime->dma_area);
	dev_dbg(marian->card->dev, "  dma_addr  : 0x%lx\n",
		(unsigned long)substream->runtime->dma_addr);
	dev_dbg(marian->card->dev, "  dma_bytes : 0x%lx (%d)\n",
		(unsigned long)substream->runtime->dma_bytes,
					(int)substream->runtime->dma_bytes);
	dev_dbg(marian->card->dev, "  dma_buffer: 0x%lx\n",
		(unsigned long)substream->runtime->dma_buffer_p);

	if (marian->desc->prepare)
		marian->desc->prepare(marian);

	// if there is card specific codec initialization, call it
	if (marian->desc->init_codec)
		marian->desc->init_codec(marian);

	return 0;
}

static void marian_silence(struct marian_card *marian)
{
	memset(marian->dmabuf.area, 0, marian->dmabuf.bytes);
}

static int snd_marian_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct marian_card *marian = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		marian_silence(marian);
		dev_dbg(marian->card->dev, "  enabling DMA transfers\n");
		writel(0x3, marian->iobase + SERAPH_WR_DMA_ENABLE);
		dev_dbg(marian->card->dev, "  enabling IRQ\n");
		writel(0x2, marian->iobase + SERAPH_WR_IE_ENABLE);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dev_dbg(marian->card->dev, "  disabling IRQ\n");
		writel(0x0, marian->iobase + SERAPH_WR_IE_ENABLE);
		dev_dbg(marian->card->dev, "  disabling DMA transfers\n");
		writel(0x0, marian->iobase + SERAPH_WR_DMA_ENABLE);
		marian_silence(marian);

		// unarm channels to inhibit playback from the FPGA's internal buffer
		writel(0, marian->iobase + 0x08);
		writel(0, marian->iobase + 0x0C);
		writel(0, marian->iobase + 0x20);
		writel(0, marian->iobase + 0x24);
		writel(0, marian->iobase + 0x28);
		writel(0, marian->iobase + 0x2C);
		writel(0, marian->iobase + 0x30);
		writel(0, marian->iobase + 0x34);
		writel(0, marian->iobase + 0x38);
		writel(0, marian->iobase + 0x3C);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t snd_marian_hw_pointer(struct snd_pcm_substream *substream)
{
	struct marian_card *marian = snd_pcm_substream_chip(substream);

	return readl(marian->iobase + SERAPH_RD_HWPOINTER);
}

/*
 * Capture and playback data lie one after another in the buffer.
 * The start position of the playback area and the length of each
 * channel's buffer depend directly on the period size.
 * We give ALSA the same dmabuf for playback and capture and set
 * each channel's buffer position using snd_pcm_channel_info->first.
 */
static int marian_channel_info(struct snd_pcm_substream *substream,
			       struct snd_pcm_channel_info *info)
{
	struct marian_card *marian = snd_pcm_substream_chip(substream);

	info->offset = 0;
	info->first = (((substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? 1 : 0)
		* marian->period_size * marian->desc->dma_ch_offset * 4
		+ info->channel * marian->period_size * 4) * 8;
	info->step = 32;

	return 0;
}

static int snd_marian_ioctl(struct snd_pcm_substream *substream, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case SNDRV_PCM_IOCTL1_CHANNEL_INFO:
		return marian_channel_info(substream, (struct snd_pcm_channel_info *)arg);
	}

	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_marian_mmap(struct snd_pcm_substream *substream, struct vm_area_struct *vma)
{
	struct marian_card *marian = snd_pcm_substream_chip(substream);

	dev_dbg(marian->card->dev,
		"(%d, %016lx)\n", substream->stream, vma->vm_start);
	dev_dbg(marian->card->dev,
		"  substream->runtime.dma_addr = %pad\n  substream->runtime.dma_area = %p\n  substream->runtime.dma_bytes = %zu\n",
		&substream->runtime->dma_addr, substream->runtime->dma_area,
		substream->runtime->dma_bytes);
	dev_dbg(marian->card->dev,
		"  vma->vm_start = %016lx\n  vma->vm_end = %016lx\n",
		vma->vm_start, vma->vm_end);

	if (remap_pfn_range(vma, vma->vm_start, substream->runtime->dma_addr >> PAGE_SHIFT,
			    vma->vm_end - vma->vm_start, vma->vm_page_prot) < 0) {
		dev_err(marian->card->dev, "remap_pfn_range failed\n");
		return -EIO;
	}

	return 0;
}

static const struct snd_pcm_ops snd_marian_playback_ops = {
	.open = snd_marian_playback_open,
	.close = snd_marian_playback_release,
	.hw_params = snd_marian_hw_params,
	.prepare = snd_marian_prepare,
	.trigger = snd_marian_trigger,
	.pointer = snd_marian_hw_pointer,
	.ioctl = snd_marian_ioctl,
	.mmap = snd_marian_mmap,
};

static const struct snd_pcm_ops snd_marian_capture_ops = {
	.open = snd_marian_capture_open,
	.close = snd_marian_capture_release,
	.hw_params = snd_marian_hw_params,
	.prepare = snd_marian_prepare,
	.trigger = snd_marian_trigger,
	.pointer = snd_marian_hw_pointer,
	.ioctl = snd_marian_ioctl,
	.mmap = snd_marian_mmap,
};

static void marian_generic_set_clock_source(struct marian_card *marian, u8 source)
{
	writel(source, marian->iobase + 0x90);
	marian->clock_source = source;
}

static void marian_generic_init(struct marian_card *marian)
{
	if (!marian->desc->set_speedmode)
		marian->desc->set_speedmode = marian_generic_set_speedmode;

	// reset DMA engine
	writel(0x00000000, marian->iobase);

	// disable play interrupt
	writel(0x02, marian->iobase + 0xAC);

	marian_generic_set_dco(marian, 48000, 0);

	// init clock mode
	marian_generic_set_speedmode(marian, SPEEDMODE_SLOW);

	// init internal clock and set it as clock source
	marian_generic_set_clock_source(marian, 1);

	// init SPI clock divider
	writel(0x1F, marian->iobase + 0x74);
}

static int snd_marian_create(struct snd_card *card, struct pci_dev *pci,
			     struct marian_card_descriptor *desc, unsigned int idx)
{
	struct snd_info_entry *entry;
	struct marian_card *marian = card->private_data;
	int err;
	unsigned int len;

	marian->desc = desc;
	marian->card = card;
	marian->pcm = NULL;
	marian->pci = pci;
	marian->port = 0;
	marian->iobase = NULL;
	marian->irq = -1;
	marian->idx = idx;
	spin_lock_init(&marian->lock);

	err = pci_enable_device(pci);
	if (err < 0)
		return err;
	// TODO set dma mask here
	pci_set_master(pci);

	err = pci_request_regions(pci, "marian");
	if (err < 0)
		return err;

	marian->port = pci_resource_start(pci, 0);
	len = pci_resource_len(pci, 0);
	dev_dbg(&pci->dev, "grabbing memory region 0x%lx-0x%lx (%d bytes)\n",
		marian->port, marian->port + len - 1, len);
	marian->iobase = pci_iomap(pci, 0, 0);
	if (!marian->iobase) {
		dev_err(&pci->dev, "unable to grab region 0x%lx-0x%lx\n",
			marian->port, marian->port + len - 1);
		return -EBUSY;
	}

	dev_dbg(&pci->dev, "grabbing IRQ %d\n", pci->irq);
	if (request_irq(pci->irq, snd_marian_interrupt, IRQF_SHARED, "marian", marian)) {
		dev_err(&pci->dev, "unable to grab IRQ %d\n", pci->irq);
		return -EBUSY;
	}
	marian->irq = pci->irq;

	card->private_free = snd_marian_card_free;

	strcpy(card->driver, "MARIAN FPGA");
	strcpy(card->shortname, marian->desc->name);
	sprintf(card->longname, "%s PCIe audio at 0x%lx, irq %d",
		card->shortname, marian->port, marian->irq);

	snd_card_set_dev(card, &pci->dev);

	err = snd_pcm_new(card, desc->name, 0, 1, 1, &marian->pcm);
	if (err < 0)
		return err;
	marian->pcm->private_data = marian;
	snd_pcm_set_ops(marian->pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_marian_playback_ops);
	snd_pcm_set_ops(marian->pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_marian_capture_ops);

	len = PAGE_ALIGN(desc->dma_bufsize);
	dev_dbg(card->dev, "Allocating %d bytes DMA buffer\n", len);
	err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, &pci->dev,
				  desc->dma_bufsize, &marian->dmabuf);
	if (err < 0) {
		dev_err(card->dev, "Could not allocate %d Bytes (%d)\n", len, err);
		while (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, &pci->dev, len, &marian->dmabuf) < 0)
			len--; // TODO Fix logical bug (what if we can't allocate any block?)

		snd_dma_free_pages(&marian->dmabuf);
		dev_err(card->dev,
			"The maximum block of consecutive bytes is %d bytes long.\n", len);
		dev_err(card->dev, "Please reboot to clean your page table.\n");
		return err;
	}

	dev_dbg(card->dev, "dmabuf.area = 0x%lx\n",
		(unsigned long)marian->dmabuf.area); // TODO fix address conversions
	dev_dbg(card->dev, "dmabuf.addr = 0x%lx\n", (unsigned long)marian->dmabuf.addr);
	dev_dbg(card->dev, "dmabuf.bytes = %d\n", (int)marian->dmabuf.bytes);

	if (!snd_card_proc_new(card, "status", &entry))
		snd_info_set_text_ops(entry, marian, snd_marian_proc_status);
	if (!snd_card_proc_new(card, "ports.in", &entry))
		snd_info_set_text_ops(entry, marian, snd_marian_proc_ports_in);
	if (!snd_card_proc_new(card, "ports.out", &entry))
		snd_info_set_text_ops(entry, marian, snd_marian_proc_ports_out);

	if (marian->desc->init_card)
		marian->desc->init_card(marian);
	else
		marian_generic_init(marian);

	if (marian->desc->create_controls)
		marian->desc->create_controls(marian);

	return snd_card_register(card);
}

/*
 * Measure the frequency of a clock source.
 * The measurement is triggered and the FPGA's ready
 * signal polled (normally takes up to 2ms). The measurement
 * has only a certainty of 10-20Hz, this function rounds it up
 * to the nearest 10Hz step (in 1FS).
 */
static unsigned int marian_measure_freq(struct marian_card *marian, unsigned int source)
{
	u32 val;
	int tries = 5;

	writel(source & 0x7, marian->iobase + 0xC8);

	while (tries > 0) {
		val = readl(marian->iobase + 0x94);
		if (val & WCLOCK_NEW_VAL)
			break;

		msleep(1);
		tries--;
	}

	if (tries > 0)
		return (((1280000000 / ((val & 0x3FFFF) + 1)) + 5 * marian->speedmode)
		/ (10 * marian->speedmode)) * 10 * marian->speedmode;

	return 0;
}

static int marian_generic_frequency_info(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 27000;
	uinfo->value.integer.max = 207000;
	uinfo->value.integer.step = 1;
	return 0;
}

static int marian_generic_frequency_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = marian_measure_freq(marian, kcontrol->private_value);
	return 0;
}

static int marian_generic_frequency_create(struct marian_card *marian, char *label, u32 idx)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.private_value = idx,
		.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_generic_frequency_info,
		.get = marian_generic_frequency_get
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static int marian_generic_dco_int_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	if (marian->speedmode == SPEEDMODE_SLOW) {
		uinfo->value.integer.min = 32000;
		uinfo->value.integer.max = 54000;
	}
	uinfo->value.integer.step = 1;
	return 0;
}

static int marian_generic_dco_int_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = marian->dco;

	return 0;
}

static int marian_generic_dco_int_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	marian_generic_set_dco(marian, ucontrol->value.integer.value[0], marian->dco_millis);

	return 0;
}

static int marian_generic_dco_int_create(struct marian_card *marian, char *label)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_generic_dco_int_info,
		.get = marian_generic_dco_int_get,
		.put = marian_generic_dco_int_put
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static int marian_generic_dco_millis_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 999;
	uinfo->value.integer.step = 1;

	return 0;
}

static int marian_generic_dco_millis_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = marian->dco_millis;

	return 0;
}

static int marian_generic_dco_millis_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	marian_generic_set_dco(marian, marian->dco, ucontrol->value.integer.value[0]);

	return 0;
}

static int marian_generic_dco_millis_create(struct marian_card *marian, char *label)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_generic_dco_millis_info,
		.get = marian_generic_dco_millis_get,
		.put = marian_generic_dco_millis_put
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static int marian_generic_dco_detune_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = -200;
	uinfo->value.integer.max = 200;
	uinfo->value.integer.step = 1;
	return 0;
}

static int marian_generic_dco_detune_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = marian->detune;

	return 0;
}

static int marian_generic_dco_detune_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	marian->detune = ucontrol->value.integer.value[0];

	marian_generic_set_dco(marian, marian->dco, marian->dco_millis);

	return 0;
}

static int marian_generic_dco_detune_create(struct marian_card *marian, char *label)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_generic_dco_detune_info,
		.get = marian_generic_dco_detune_get,
		.put = marian_generic_dco_detune_put
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static void marian_generic_dco_create(struct marian_card *marian)
{
	marian_generic_dco_int_create(marian, "DCO Freq (Hz)");
	marian_generic_dco_millis_create(marian, "DCO Freq (millis)");
	marian_generic_dco_detune_create(marian, "DCO Detune (cent)");
}

static int marian_generic_speedmode_info(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_info *uinfo)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);
	static const char * const texts[] = { "1FS", "2FS", "4FS" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	switch (marian->desc->speedmode_max) {
	case SPEEDMODE_SLOW:
		uinfo->value.enumerated.items = 1;
		break;
	case SPEEDMODE_NORMAL:
		uinfo->value.enumerated.items = 2;
		break;
	case SPEEDMODE_FAST:
		uinfo->value.enumerated.items = 3;
		break;
	}
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int marian_generic_speedmode_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	if (marian->speedmode == SPEEDMODE_FAST)
		ucontrol->value.enumerated.item[0] = 2;
	else
		ucontrol->value.enumerated.item[0] = marian->speedmode - 1;

	return 0;
}

static int marian_generic_speedmode_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	if (ucontrol->value.enumerated.item[0] < 2)
		marian->desc->set_speedmode(marian, ucontrol->value.enumerated.item[0] + 1);
	else
		marian->desc->set_speedmode(marian, SPEEDMODE_FAST);

	return 0;
}

static int marian_generic_speedmode_create(struct marian_card *marian)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Speed Mode",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_generic_speedmode_info,
		.get = marian_generic_speedmode_get,
		.put = marian_generic_speedmode_put,
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static int spi_wait_for_ar(struct marian_card *marian)
{
	int tries = 10;

	while (tries > 0) {
		if (readl(marian->iobase + 0x70) == SPI_ALL_READY)
			break;
		msleep(1);
		tries--;
	}
	if (tries == 0)
		return -EIO;
	return 0;
}

static int marian_spi_transfer(struct marian_card *marian, uint16_t cs, uint16_t bits_write,
			u8 *data_write, uint16_t bits_read, u8 *data_read)
{
	u32 buf = 0;
	unsigned int i;

	dev_dbg(marian->card->dev,
		"(.., 0x%04x, %u, [%02x, %02x], %u, ..)\n", cs, bits_write,
		data_write[0], data_write[1], bits_read);

	if (spi_wait_for_ar(marian) < 0) {
		dev_dbg(marian->card->dev, "Resetting SPI bus\n");
		writel(0x1234, marian->iobase + 0x70);
	}

	writel(cs, marian->iobase + 0x60);         // chip select register
	writel(bits_write, marian->iobase + 0x64); // number of bits to write
	writel(bits_read, marian->iobase + 0x68);  // number of bits to read

	if (bits_write <= 32) {
		if (bits_write <= 8)
			buf = data_write[0] << (32 - bits_write);
		else if (bits_write <= 16)
			buf = data_write[0] << 24 | data_write[1] << (32 - bits_write);

		writel(buf, marian->iobase + 0x6C); // write data left aligned
	}
	if (bits_read > 0 && bits_read <= 32) {
		if (spi_wait_for_ar(marian) < 0) {
			dev_dbg(marian->card->dev,
				"Bus didn't signal AR\n");
			return -EIO;
		}

		buf = readl(marian->iobase + 0x74);

		buf <<= 32 - bits_read;
		i = 0;

		while (bits_read > 0) {
			data_read[i++] = (buf >> 24) & 0xFF;
			buf <<= 8;
			bits_read -= 8;
		}
	}

	return 0;
}

static u8 marian_m2_spi_read(struct marian_card *marian, u8 adr)
{
	u8 buf_in;

	adr = adr & 0x7F;

	if (marian_spi_transfer(marian, 0x02, 8, &adr, 8, &buf_in) == 0)
		return buf_in;

	return 0;
}

static int marian_m2_spi_write(struct marian_card *marian, u8 adr, u8 val)
{
	u8 buf_out[2];

	buf_out[0] = 0x80 | adr;
	buf_out[1] = val;

	return marian_spi_transfer(marian, 0x02, 16, (u8 *)&buf_out, 0, NULL);
}

static int marian_m2_sync_state_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = { "No Signal", "Lock", "Sync" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = ARRAY_SIZE(texts);
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int marian_m2_sync_state_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);
	u8 v = marian_m2_spi_read(marian, 0x00);

	v = (v >> (kcontrol->private_value * 2)) & 0x3;
	if (v == 3)
		v--;

	ucontrol->value.enumerated.item[0] = v;

	return 0;
}

static int marian_m2_sync_state_create(struct marian_card *marian, char *label, u32 idx)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.private_value = idx,
		.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_m2_sync_state_info,
		.get = marian_m2_sync_state_get
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static int marian_m2_channel_mode_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = { "56ch", "64ch" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = ARRAY_SIZE(texts);
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int marian_m2_input_channel_mode_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);
	u8 v = marian_m2_spi_read(marian, 0x01);

	v = (v >> (kcontrol->private_value * 2)) & 0x1;
	ucontrol->value.enumerated.item[0] = v;

	return 0;
}

static int marian_m2_input_channel_mode_create(struct marian_card *marian,
					       char *label, u32 idx)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.private_value = idx,
		.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_m2_channel_mode_info,
		.get = marian_m2_input_channel_mode_get
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static int marian_m2_frame_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = { "48kHz", "96kHz" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = ARRAY_SIZE(texts);
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int marian_m2_input_frame_mode_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);
	u8 v = marian_m2_spi_read(marian, 0x01);

	v = (v >> ((kcontrol->private_value * 2) + 1)) & 0x1;
	ucontrol->value.enumerated.item[0] = v;

	return 0;
}

static int marian_m2_input_frame_mode_create(struct marian_card *marian, char *label, u32 idx)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.private_value = idx,
		.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_m2_frame_mode_info,
		.get = marian_m2_input_frame_mode_get
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static u8 marian_m2_get_port_mode(struct marian_card *marian, unsigned int port)
{
	struct m2_specific *spec = marian->card_specific;

	if (port)
		return (spec->shadow_42 >> M2_PORT2_MODE) & 1;
	else
		return (spec->shadow_42 >> M2_PORT1_MODE) & 1;
}

static int marian_m2_output_channel_mode_get(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = marian_m2_get_port_mode(marian,
								     kcontrol->private_value);

	return 0;
}

static void marian_m2_set_port_mode(struct marian_card *marian, unsigned int port, u8 state)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	if (port)
		spec->shadow_42 = (spec->shadow_42 & ~(1 << M2_PORT2_MODE))
				| state << M2_PORT2_MODE;
	else
		spec->shadow_42 = (spec->shadow_42 & ~(1 << M2_PORT1_MODE))
				| state << M2_PORT1_MODE;

	marian_m2_spi_write(marian, 0x42, spec->shadow_42);
}

static int marian_m2_output_channel_mode_put(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	marian_m2_set_port_mode(marian, kcontrol->private_value,
				ucontrol->value.enumerated.item[0]);

	return 0;
}

static int marian_m2_output_channel_mode_create(struct marian_card *marian,
						char *label, u32 idx)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.private_value = idx,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_m2_channel_mode_info,
		.get = marian_m2_output_channel_mode_get,
		.put = marian_m2_output_channel_mode_put
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static int marian_m2_output_frame_mode_info(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}

static int marian_m2_output_frame_mode_get(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	ucontrol->value.enumerated.item[0] = (spec->frame >> kcontrol->private_value) & 1;

	return 0;
}

static void marian_m2_write_port_frame(struct marian_card *marian)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	spec->shadow_42 = spec->shadow_42 & ~((1 << M2_PORT1_FRAME) | (1 << M2_PORT2_FRAME));

	if (marian->speedmode == 2) {
		// If we are in FS2, set 96kHz mode where enabled
		if (spec->frame & 1)
			spec->shadow_42 |= 1 << M2_PORT1_FRAME;
		if (spec->frame & 2)
			spec->shadow_42 |= 1 << M2_PORT2_FRAME;
	}

	marian_m2_spi_write(marian, 0x42, spec->shadow_42);
}

static void marian_m2_set_port_frame(struct marian_card *marian, unsigned int port, u8 state)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	spec->frame = (spec->frame & ~(1 << port)) | (state << port);

	marian_m2_write_port_frame(marian);
}

static int marian_m2_output_frame_mode_put(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	dev_dbg(marian->card->dev, "%lu -> %u\n",
		kcontrol->private_value, ucontrol->value.enumerated.item[0]);

	marian_m2_set_port_frame(marian, kcontrol->private_value,
				 ucontrol->value.enumerated.item[0]);

	return 0;
}

static int marian_m2_output_frame_mode_create(struct marian_card *marian, char *label, u32 idx)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.private_value = idx,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_m2_output_frame_mode_info,
		.get = marian_m2_output_frame_mode_get,
		.put = marian_m2_output_frame_mode_put,
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static int marian_m2_clock_source_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = {"Internal", "Sync Bus",
					      "Input Port 1", "Input Port 2"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = ARRAY_SIZE(texts);
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int marian_m2_clock_source_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	switch (marian->clock_source) {
	case M2_CLOCK_SRC_DCO:
		ucontrol->value.enumerated.item[0] = CLOCK_SRC_INTERNAL;
		break;
	case M2_CLOCK_SRC_SYNCBUS:
		ucontrol->value.enumerated.item[0] = CLOCK_SRC_SYNCBUS;
		break;
	case M2_CLOCK_SRC_MADI1:
		ucontrol->value.enumerated.item[0] = CLOCK_SRC_INP1;
		break;
	case M2_CLOCK_SRC_MADI2:
		ucontrol->value.enumerated.item[0] = CLOCK_SRC_INP2;
		break;
	default:
		dev_dbg(marian->card->dev,
			"Illegal value for clock_source! (%d)\n",
			marian->clock_source);
		return -EINVAL;
	}

	return 0;
}

static int marian_m2_clock_source_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	switch (ucontrol->value.enumerated.item[0]) {
	case CLOCK_SRC_INTERNAL:
		marian_generic_set_clock_source(marian, M2_CLOCK_SRC_DCO);
		break;
	case CLOCK_SRC_SYNCBUS:
		marian_generic_set_clock_source(marian, M2_CLOCK_SRC_SYNCBUS);
		break;
	case CLOCK_SRC_INP1:
		marian_generic_set_clock_source(marian, M2_CLOCK_SRC_MADI1);
		break;
	case CLOCK_SRC_INP2:
		marian_generic_set_clock_source(marian, M2_CLOCK_SRC_MADI2);
		break;
	}

	return 0;
}

static int marian_m2_clock_source_create(struct marian_card *marian)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Clock Source",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_m2_clock_source_info,
		.get = marian_m2_clock_source_get,
		.put = marian_m2_clock_source_put,
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

/*
 * Controls:
 *
 * RO:
 *   - Input 1 sync state (no signal, lock, sync)
 *   - Input 1 channel mode (56/64ch)
 *   - Input 1 frame mode (48/96kHz)
 *   - Input 1 frequency
 *   - Input 2 sync state (no signal, lock, sync)
 *   - Input 2 channel mode (56/64ch)
 *   - Input 2 frame mode (48/96kHz)
 *   - Input 2 frequency
 *
 * RW:
 *   - Output 1 channel mode (56/64ch)
 *   - Output 1 frame mode (48/96kHz)
 *   - Output 2 channel mode (56/64ch)
 *   - Output 2 frame mode (48/96kHz)
 *   - Word clock source (Port 1, Port 2, Internal, Sync port, WCK input)
 *   - Speed mode (1, 2, 4FS)
 *   - DCO frequency (1 Hertz)
 *   - DCO frequency (1/1000th)
 */
static void marian_m2_create_controls(struct marian_card *marian)
{
	marian_m2_sync_state_create(marian, "Input 1 Sync", M2_INP1_SYNC_CTL_ID);
	marian_m2_sync_state_create(marian, "Input 2 Sync", M2_INP2_SYNC_CTL_ID);
	marian_m2_input_channel_mode_create(marian, "Input 1 Channel Mode",
					    M2_INP1_CM_CTL_ID);
	marian_m2_input_channel_mode_create(marian, "Input 2 Channel Mode",
					    M2_INP2_CM_CTL_ID);
	marian_m2_input_frame_mode_create(marian, "Input 1 Frame Mode",
					  M2_INP1_FM_CTL_ID);
	marian_m2_input_frame_mode_create(marian, "Input 2 Frame Mode",
					  M2_INP2_FM_CTL_ID);
	marian_generic_frequency_create(marian, "Input 1 Frequency",
					M2_INP1_FREQ_CTL_ID);
	marian_generic_frequency_create(marian, "Input 2 Frequency",
					M2_INP2_FREQ_CTL_ID);
	marian_m2_output_channel_mode_create(marian, "Output 1 Channel Mode",
					     M2_OUT1_CM_CTL_ID);
	marian_m2_output_channel_mode_create(marian, "Output 2 Channel Mode",
					     M2_OUT2_CM_CTL_ID);
	marian_m2_output_frame_mode_create(marian, "Output 1 96kHz Frame",
					   M2_OUT1_FM_CTL_ID);
	marian_m2_output_frame_mode_create(marian, "Output 2 96kHz Frame",
					   M2_OUT2_FM_CTL_ID);
	marian_m2_clock_source_create(marian);
	marian_generic_speedmode_create(marian);
	marian_generic_dco_create(marian);
}

static void marian_m2_set_float(struct marian_card *marian, enum m2_num_mode state)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	spec->shadow_41 = (spec->shadow_41 & ~(1 << M2_INT_FLOAT)) | state << M2_INT_FLOAT;
	marian_m2_spi_write(marian, 0x41, spec->shadow_41);
}

static void marian_m2_set_endianness(struct marian_card *marian, enum m2_endianness_mode state)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	spec->shadow_41 = (spec->shadow_41 & ~(1 << M2_ENDIANNESS)) | state << M2_ENDIANNESS;
	marian_m2_spi_write(marian, 0x41, spec->shadow_41);
}

static void marian_m2_set_speedmode(struct marian_card *marian, unsigned int speedmode)
{
	marian_generic_set_speedmode(marian, speedmode);
	marian_m2_write_port_frame(marian);
}

static int marian_m2_init(struct marian_card *marian)
{
	struct m2_specific *spec;

	marian_generic_init(marian);
	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		dev_dbg(marian->card->dev,
			"Cannot allocate card specific structure\n");
		return -ENOMEM;
	}

	spec->shadow_40 = 0x00;
	spec->shadow_41 = (1 << M2_TX_ENABLE);
	spec->shadow_42 = (1 << M2_PORT1_MODE) | (1 << M2_PORT2_MODE);

	marian_m2_spi_write(marian, 0x40, spec->shadow_40);
	marian_m2_spi_write(marian, 0x41, spec->shadow_41);
	marian_m2_spi_write(marian, 0x42, spec->shadow_42);

	marian->card_specific = spec;
	return 0;
}

static void marian_m2_free(struct marian_card *marian)
{
	kfree(marian->card_specific);
}

static void marian_m2_prepare(struct marian_card *marian)
{
	u32 mask = 0xFFFFFFFF;

	writel(mask, marian->iobase + 0x20);
	writel(mask, marian->iobase + 0x24);
	writel(mask, marian->iobase + 0x28);
	writel(mask, marian->iobase + 0x2C);
	writel(mask, marian->iobase + 0x30);
	writel(mask, marian->iobase + 0x34);
	writel(mask, marian->iobase + 0x38);
	writel(mask, marian->iobase + 0x3C);
}

static void marian_m2_proc_status(struct marian_card *marian, struct snd_info_buffer *buffer)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;
	u8 v1, v2;

	marian_proc_status_generic(marian, buffer);

	snd_iprintf(buffer, "\n*** MADI FPGA registers\n");
	snd_iprintf(buffer, "M2 MADI 00h: %02x\n", marian_m2_spi_read(marian, 0x00));
	snd_iprintf(buffer, "M2 MADI 01h: %02x\n", marian_m2_spi_read(marian, 0x01));
	snd_iprintf(buffer, "M2 MADI 02h: %02x\n", marian_m2_spi_read(marian, 0x02));
	snd_iprintf(buffer, "M2 MADI 40h: %02x\n", spec->shadow_40);
	snd_iprintf(buffer, "M2 MADI 41h: %02x\n", spec->shadow_41);
	snd_iprintf(buffer, "M2 MADI 42h: %02x\n", spec->shadow_42);

	snd_iprintf(buffer, "\n*** MADI FPGA status\n");
	snd_iprintf(buffer, "MADI FPGA firmware: 0x%02x\n", marian_m2_spi_read(marian, 0x02));

	snd_iprintf(buffer, "Clock source: ");
	switch (marian->clock_source) {
	case 1:
		snd_iprintf(buffer, "Internal DCO\n");
		break;
	case 2:
		snd_iprintf(buffer, "Sync bus\n");
		break;
	case 4:
		snd_iprintf(buffer, "MADI Input 1\n");
		break;
	case 5:
		snd_iprintf(buffer, "MADI Input 2\n");
		break;
	default:
		snd_iprintf(buffer, "UNKNOWN\n");
		break;
	}

	snd_iprintf(buffer, "Sample format: %s, %s Endian, %s first\n",
		    (spec->shadow_41 & (1 << M2_INT_FLOAT)) ? "Float" : "Integer",
		    (spec->shadow_41 & (1 << M2_ENDIANNESS)) ? "Little" : "Big",
		    (spec->shadow_41 & (1 << M2_BIT_ORDER)) ? "LSB" : "MSB");

	v1 = marian_m2_spi_read(marian, 0x00);
	v2 = marian_m2_spi_read(marian, 0x01);

	snd_iprintf(buffer, "MADI port 1 input: ");
	if (!(v1 & 0x03))
		snd_iprintf(buffer, "No signal\n");
	else
		snd_iprintf(buffer, "%s, %dch, %dkHz frame, %u Hz\n",
			    (v1 & 0x02) ? "sync" : "lock", (v2 & 0x01) ? 64 : 56,
			    (v2 & 0x02) ? 96 : 48, marian_measure_freq(marian, 4));

	snd_iprintf(buffer, "MADI port 2 input: ");
	if (!(v1 & 0x0C))
		snd_iprintf(buffer, "No signal\n");
	else
		snd_iprintf(buffer, "%s, %dch, %dkHz frame, %u Hz\n",
			    (v1 & 0x08) ? "sync" : "lock",
			    (v2 & 0x04) ? 64 : 56, (v2 & 0x08) ? 96 : 48,
			    marian_measure_freq(marian, 5));
}

static void marian_m2_proc_ports(struct marian_card *marian,
			  struct snd_info_buffer *buffer, unsigned int type)
{
	int i;

	for (i = 0; i < 128; i++)
		snd_iprintf(buffer, "%d=MADI p%dch%02d\n", i + 1, i / 64 + 1, i % 64 + 1);
}

static void marian_m2_constraints(struct marian_card *marian, struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params)
{
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_FLOAT_BE:
		marian_m2_set_float(marian, M2_NUM_MODE_FLOAT);
		marian_m2_set_endianness(marian, M2_BE);
		break;
	case SNDRV_PCM_FORMAT_FLOAT_LE:
		marian_m2_set_float(marian, M2_NUM_MODE_FLOAT);
		marian_m2_set_endianness(marian, M2_LE);
		break;
	case SNDRV_PCM_FORMAT_S32_BE:
		marian_m2_set_float(marian, M2_NUM_MODE_INT);
		marian_m2_set_endianness(marian, M2_BE);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		marian_m2_set_float(marian, M2_NUM_MODE_INT);
		marian_m2_set_endianness(marian, M2_LE);
		break;
	}
}

static struct marian_card_descriptor descriptor = {
	.name = "Seraph M2",
	.speedmode_max = 2,
	.ch_in = 128,
	.ch_out = 128,
	.dma_ch_offset = 128,
	.dma_bufsize = 2 * 128 * 2 * 2048 * 4,
	.hw_constraints_func = marian_m2_constraints,
	.create_controls = marian_m2_create_controls,
	.init_card = marian_m2_init,
	.free_card = marian_m2_free,
	.prepare = marian_m2_prepare,
	.set_speedmode = marian_m2_set_speedmode,
	.proc_status = marian_m2_proc_status,
	.proc_ports = marian_m2_proc_ports,
	.info_playback = {
		.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_NONINTERLEAVED
			| SNDRV_PCM_INFO_JOINT_DUPLEX | SNDRV_PCM_INFO_SYNC_START,
		.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE
			| SNDRV_PCM_FMTBIT_FLOAT_LE | SNDRV_PCM_FMTBIT_FLOAT_BE,
		.rates = (SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_44100
			| SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200
			| SNDRV_PCM_RATE_96000),
		.rate_min = 28000,
		.rate_max = 113000,
		.channels_min = 128,
		.channels_max = 128,
		.buffer_bytes_max = 2 * 128 * 2 * 1024 * 4,
		.period_bytes_min = 16 * 4,
		.period_bytes_max = 1024 * 4 * 128,
		.periods_min = 2,
		.periods_max = 2
	},
	.info_capture = {
		.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_NONINTERLEAVED
			| SNDRV_PCM_INFO_SYNC_START | SNDRV_PCM_INFO_JOINT_DUPLEX,
		.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE
			| SNDRV_PCM_FMTBIT_FLOAT_LE | SNDRV_PCM_FMTBIT_FLOAT_BE,
		.rates = (SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_44100
			| SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200
			| SNDRV_PCM_RATE_96000),
		.rate_min = 28000,
		.rate_max = 113000,
		.channels_min = 128,
		.channels_max = 128,
		.buffer_bytes_max = 2 * 128 * 2 * 1024 * 4,
		.period_bytes_min = 16 * 4,
		.period_bytes_max = 1024 * 4 * 128,
		.periods_min = 2,
		.periods_max = 2
	}
};

static int snd_marian_m2_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	static unsigned int dev;
	struct snd_card *card;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	err = snd_card_new(&pci->dev, index[dev], id[dev],
			   THIS_MODULE, sizeof(struct marian_card), &card);
	if (err < 0)
		return err;

	err = snd_marian_create(card, pci, &descriptor, dev);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	pci_set_drvdata(pci, card);

	dev++;

	return 0;
}

static void snd_marian_m2_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver marian_driver = {
	.name = "MARIAN",
	.id_table = snd_marian_ids,
	.probe = snd_marian_m2_probe,
	.remove = snd_marian_m2_remove,
};

module_pci_driver(marian_driver);

MODULE_AUTHOR("Florin Faber <faberman@linuxproaudio.org>");
MODULE_DESCRIPTION("MARIAN Seraph series");
MODULE_LICENSE("GPL");
