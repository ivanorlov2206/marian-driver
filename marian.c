/*
 *   ALSA driver for MARIAN Seraph audio interfaces
 *
 *      Copyright (c) 2012 Florian Faber <faberman@linuxproaudio.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/version.h>

#include "common.h"
#include "generic.h"
#include "a3.h"
#include "m2.h"
#include "seraph8.h"

static int snd_marian_create(struct snd_card *card, struct pci_dev *pci,
			     struct marian_card_descriptor *desc, unsigned int idx);
static void snd_marian_card_free(struct snd_card *card);

// ALSA interface
static int snd_marian_create(struct snd_card *card, struct pci_dev *pci,
			     struct marian_card_descriptor *desc, unsigned int idx);
static void snd_marian_card_free(struct snd_card *card);
static int snd_marian_playback_open(struct snd_pcm_substream *substream);
static int snd_marian_playback_release(struct snd_pcm_substream *substream);
static int snd_marian_capture_open(struct snd_pcm_substream *substream);
static int snd_marian_capture_release(struct snd_pcm_substream *substream);
static int snd_marian_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params);
static int snd_marian_hw_free(struct snd_pcm_substream *substream);
static int snd_marian_prepare(struct snd_pcm_substream *substream);
static int snd_marian_trigger(struct snd_pcm_substream *substream, int cmd);
static snd_pcm_uframes_t snd_marian_hw_pointer(struct snd_pcm_substream *substream);
static int snd_marian_ioctl(struct snd_pcm_substream*, unsigned int, void*);

static void snd_marian_proc_status(struct snd_info_entry  *entry, struct snd_info_buffer *buffer);
static void snd_marian_proc_ports_in(struct snd_info_entry *entry, struct snd_info_buffer *buffer);
static void snd_marian_proc_ports_out(struct snd_info_entry *entry, struct snd_info_buffer *buffer);
static int snd_marian_mmap(struct snd_pcm_substream *substream, struct vm_area_struct *vma);

static struct marian_card_descriptor descriptors[7] = {
	{
		.name = "Seraph A3",
		.speedmode_max = 2,
		.ch_in = 24,
		.ch_out = 24,
		.dma_ch_offset = 32,
		.dma_bufsize = 2 * 32 * 2 * 2048 * 4,
		.create_controls = marian_a3_create_controls,
		.prepare = marian_a3_prepare,
		.init_card = marian_a3_init,
		.proc_status = marian_a3_proc_status,
		.proc_ports = marian_a3_proc_ports,
		.info_playback = {
			.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_NONINTERLEAVED
				| SNDRV_PCM_INFO_JOINT_DUPLEX | SNDRV_PCM_INFO_SYNC_START,
			.formats = SNDRV_PCM_FMTBIT_S24_3LE,
			.rates = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_44100
				| SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200
				| SNDRV_PCM_RATE_96000,
			.rate_min = 28000,
			.rate_max = 113000,
			.channels_min = 1,
			.channels_max = 24,
			.buffer_bytes_max = 2 * 24 * 2 * 4096 * 4,
			.period_bytes_min = 16 * 4,
			.period_bytes_max = 2048 * 4 * 24,
			.periods_min = 2,
			.periods_max = 2
		},
		.info_capture = {
			.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_NONINTERLEAVED
				| SNDRV_PCM_INFO_JOINT_DUPLEX | SNDRV_PCM_INFO_SYNC_START,
			.formats = SNDRV_PCM_FMTBIT_S24_3LE,
			.rates = (SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_44100
				| SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200
				| SNDRV_PCM_RATE_96000),
			.rate_min = 28000,
			.rate_max = 113000,
			.channels_min = 1,
			.channels_max = 24,
			.buffer_bytes_max = 2 * 24 * 2 * 4096 * 4,
			.period_bytes_min = 16 * 4,
			.period_bytes_max = 2048 * 4 * 24,
			.periods_min = 2,
			.periods_max = 2
		}
	},
	{
		.name = "C-Box",
		.speedmode_max = 4,
	},
	{
		.name = "Seraph AD2",
		.speedmode_max = 4,
	},
	{
		.name = "Seraph D4",
		.speedmode_max = 4,
	},
	{
		.name = "Seraph D8",
		.speedmode_max = 4,
	},
	{
		.name = "Seraph 8",
		.port_names = "1=Analogue 1\n2=Analogue 2\n3=Analogue 3\n4=Analogue 4\n"
		"5=Analogue 5\n6=Analogue 6\n7=Analogue 7\n8=Analogue 8\n",
		.speedmode_max = 4,
		.ch_in = 8,
		.ch_out = 8,
		.dma_ch_offset = 32,
		.dma_bufsize = 2 * 32 * 2 * 2048 * 4,
		.create_controls = marian_seraph8_create_controls,
		.prepare = marian_seraph8_prepare,
		.init_codec = marian_seraph8_init_codec,
		.proc_status = marian_seraph8_proc_status,
		.info_playback = {
			.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_NONINTERLEAVED
				| SNDRV_PCM_INFO_JOINT_DUPLEX | SNDRV_PCM_INFO_SYNC_START,
			.formats = SNDRV_PCM_FMTBIT_S32_LE,
			.rates = (SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_44100
				| SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200
				| SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400
				| SNDRV_PCM_RATE_192000),
			.rate_min = 28000,
			.rate_max = 216000,
			.channels_min = 1,
			.channels_max = 8,
			.buffer_bytes_max = 2 * 8 * 2 * 4096 * 4,
			.period_bytes_min = 16 * 4,
			.period_bytes_max = 2048 * 4 * 8,
			.periods_min = 2,
			.periods_max = 2
		},
		.info_capture = {
			.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_NONINTERLEAVED
				| SNDRV_PCM_INFO_JOINT_DUPLEX | SNDRV_PCM_INFO_SYNC_START,
			.formats = SNDRV_PCM_FMTBIT_S32_LE,
			.rates = (SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_44100
				| SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200
				| SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400
				| SNDRV_PCM_RATE_192000),
			.rate_min = 28000,
			.rate_max = 216000,
			.channels_min = 1,
			.channels_max = 8,
			.buffer_bytes_max = 2 * 8 * 2 * 4096 * 4,
			.period_bytes_min = 16 * 4,
			.period_bytes_max = 2048 * 4 * 8,
			.periods_min = 2,
			.periods_max = 2
		}
	},
	{
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
		.init_codec = marian_m2_init_codec,
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
	}
};

// kernel interface

MODULE_AUTHOR("Florin Faber <faberman@linuxproaudio.org>");
MODULE_DESCRIPTION("MARIAN Seraph series");
MODULE_LICENSE("GPL");

static const struct pci_device_id snd_marian_ids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_MARIAN, PCI_DEVICE_ID_MARIAN_SERAPH_A3),
	 0, 0, 0},
	{PCI_DEVICE(PCI_VENDOR_ID_MARIAN, PCI_DEVICE_ID_MARIAN_C_BOX),
	 0, 0, 1},
	{PCI_DEVICE(PCI_VENDOR_ID_MARIAN, PCI_DEVICE_ID_MARIAN_SERAPH_AD2),
	 0, 0, 2},
	{PCI_DEVICE(PCI_VENDOR_ID_MARIAN, PCI_DEVICE_ID_MARIAN_SERAPH_D4),
	 0, 0, 3},
	{PCI_DEVICE(PCI_VENDOR_ID_MARIAN, PCI_DEVICE_ID_MARIAN_SERAPH_D8),
	 0, 0, 4},
	{PCI_DEVICE(PCI_VENDOR_ID_MARIAN, PCI_DEVICE_ID_MARIAN_SERAPH_8),
	 0, 0, 5},
	{PCI_DEVICE(PCI_VENDOR_ID_MARIAN, PCI_DEVICE_ID_MARIAN_SERAPH_M2),
	 0, 0, 6},
	{0,}
};

MODULE_DEVICE_TABLE(pci, snd_marian_ids);

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX; // Index 0-MAX
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR; // ID for this card

static int snd_marian_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	static unsigned int dev;
	struct snd_card *card;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	dev_dbg(&pci->dev, "(.., [%04x:%04x, %lu])\n",
		pci_id->vendor, pci_id->device, pci_id->driver_data);
	dev_dbg(&pci->dev, "Found a %s, creating instance..\n",
		descriptors[pci_id->driver_data].name);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
	err = snd_card_create(index[dev], id[dev],
			      THIS_MODULE, sizeof(struct marian_card), &card);
#else
	err = snd_card_new(&pci->dev, index[dev], id[dev],
			   THIS_MODULE, sizeof(struct marian_card), &card);
#endif

	if (err < 0)
		return err;

	err = snd_marian_create(card, pci, &descriptors[pci_id->driver_data], dev);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	pci_set_drvdata(pci, card);

	dev++;

	return 0;
}

static void snd_marian_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "MARIAN",
	.id_table = snd_marian_ids,
	.probe = snd_marian_probe,
	.remove = snd_marian_remove,
};

static int __init alsa_card_marian_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_marian_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_marian_init);
module_exit(alsa_card_marian_exit);

// card interface

static void print_irq_status(unsigned int v)
{
	snd_printdd(KERN_DEBUG "IRQ status 0x%08x\n", v);
	if (v & ERR_DEAD_WRITE)
		snd_printdd(KERN_DEBUG "  -> ERROR, dead write (PCI wr fault)\n");
	if (v & ERR_DEAD_READ)
		snd_printdd(KERN_DEBUG "  -> ERROR, dead read (PCI rd fault)\n");
	if (v & ERR_DATA_LOST)
		snd_printdd(KERN_DEBUG "  -> ERROR, data lost (PCI transfer not complete)\n");
	if (v & ERR_PAGE_CONF)
		snd_printdd(KERN_DEBUG "  -> ERROR, page conflict (transfer not complete)\n");
	if (v & STATUS_ST_READY)
		snd_printdd(KERN_DEBUG "  -> start ready\n");
	if (v & STATUS_INT_PLAY)
		snd_printdd(KERN_DEBUG "  -> interrupt play\n");
	if (v & STATUS_INT_PPLAY)
		snd_printdd(KERN_DEBUG "  -> interrupt play page\n");
	if (v & ERR_INT_PLAY)
		snd_printdd(KERN_DEBUG "  -> ERROR, interrupt play not executed\n");
	if (v & STATUS_INT_REC)
		snd_printdd(KERN_DEBUG "  -> interrupt record\n");
	if (v & STATUS_INT_PREC)
		snd_printdd(KERN_DEBUG "  -> interrupt record page\n");
	if (v & ERR_INT_REC)
		snd_printdd(KERN_DEBUG "  -> ERROR, interrupt record not executed\n");
	if (v & STATUS_INT_PREP)
		snd_printdd(KERN_DEBUG "  -> interrupt prepare\n");
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

// ALSA interface

static const struct snd_pcm_ops snd_marian_playback_ops = {
	.open = snd_marian_playback_open,
	.close = snd_marian_playback_release,
	.hw_params = snd_marian_hw_params,
	.hw_free = snd_marian_hw_free,
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
	.hw_free = snd_marian_hw_free,
	.prepare = snd_marian_prepare,
	.trigger = snd_marian_trigger,
	.pointer = snd_marian_hw_pointer,
	.ioctl = snd_marian_ioctl,
	.mmap = snd_marian_mmap,
};

static int snd_marian_create(struct snd_card *card, struct pci_dev *pci,
			     struct marian_card_descriptor *desc, unsigned int idx)
{
	struct snd_info_entry *entry;
	struct marian_card *marian = card->private_data;
	int err;
	unsigned int len;

	// initialize marian struct
	marian->desc = desc;
	marian->card = card;
	marian->pcm = NULL;
	marian->pci = pci;
	marian->port = 0;
	marian->iobase = NULL;
	marian->irq = -1;
	marian->idx = idx;
	spin_lock_init(&marian->lock);

	// configure PCI device
	err = pci_enable_device(pci);
	if (err < 0)
		return err;

	pci_set_master(pci);

	err = pci_request_regions(pci, "marian");
	if (err < 0)
		return err;

	marian->port = pci_resource_start(pci, 0);
	len = pci_resource_len(pci, 0);
	dev_dbg(&pci->dev, "grabbing memory region 0x%lx-0x%lx (%d bytes)..\n",
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

	// set up ALSA driver part
	card->private_free = snd_marian_card_free;

	strcpy(card->driver, "MARIAN FPGA");
	strcpy(card->shortname, marian->desc->name);
	sprintf(card->longname, "%s PCIe audio at 0x%lx, irq %d",
		card->shortname, marian->port, marian->irq);

	snd_card_set_dev(card, &pci->dev);

	// set up ALSA PCM
	err = snd_pcm_new(card, desc->name, 0, 1, 1, &marian->pcm);
	if (err < 0)
		return err;
	marian->pcm->private_data = marian;
	snd_pcm_set_ops(marian->pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_marian_playback_ops);
	snd_pcm_set_ops(marian->pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_marian_capture_ops);

	// set up DMA buffers
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

	// set up /proc entries
	if (!snd_card_proc_new(card, "status", &entry))
		snd_info_set_text_ops(entry, marian, snd_marian_proc_status);
	if (!snd_card_proc_new(card, "ports.in", &entry))
		snd_info_set_text_ops(entry, marian, snd_marian_proc_ports_in);
	if (!snd_card_proc_new(card, "ports.out", &entry))
		snd_info_set_text_ops(entry, marian, snd_marian_proc_ports_out);

	// if there is card specific initialization, code call it
	if (marian->desc->init_card)
		marian->desc->init_card(marian);
	else
		marian_generic_init(marian);

	// set up controls
	if (marian->desc->create_controls)
		marian->desc->create_controls(marian);

	// finally: register card
	return snd_card_register(card);
}

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

static int snd_marian_playback_open(struct snd_pcm_substream *substream)
{
	struct marian_card *marian = substream->private_data;

	substream->runtime->hw = marian->desc->info_playback;

	marian->playback_substream = substream;

	snd_pcm_set_sync(substream);

	return 0;
}

static int snd_marian_playback_release(struct snd_pcm_substream *substream)
{
	struct marian_card *marian = snd_pcm_substream_chip(substream);

	marian->playback_substream = NULL;

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
		WRITEL(marian->capture_substream->runtime->dma_addr,
		       marian->iobase + SERAPH_WR_DMA_ADR); // TODO Fix address conversions
	} else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dev_dbg(marian->card->dev, "  setting card's DMA ADR to %08x\n",
			(unsigned int)marian->playback_substream->runtime->dma_addr);
		WRITEL(marian->playback_substream->runtime->dma_addr,
		       marian->iobase + SERAPH_WR_DMA_ADR);
	}
	dev_dbg(marian->card->dev,
		"  setting card's DMA block count to %d\n", marian->period_size / 16);
	WRITEL(marian->period_size / 16, marian->iobase + SERAPH_WR_DMA_BLOCKS);

	// apply optional card specific hw constraints
	if (marian->desc->hw_constraints_func)
		marian->desc->hw_constraints_func(marian, substream, params);

	return 0;
}

static int snd_marian_hw_free(struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		// mute inputs

	} else {
		// mute outputs

	}

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
		WRITEL(0x3, marian->iobase + SERAPH_WR_DMA_ENABLE);
		dev_dbg(marian->card->dev, "  enabling IRQ\n");
		WRITEL(0x2, marian->iobase + SERAPH_WR_IE_ENABLE);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dev_dbg(marian->card->dev, "  disabling IRQ\n");
		WRITEL(0x0, marian->iobase + SERAPH_WR_IE_ENABLE);
		dev_dbg(marian->card->dev, "  disabling DMA transfers\n");
		WRITEL(0x0, marian->iobase + SERAPH_WR_DMA_ENABLE);
		marian_silence(marian);

		// unarm channels to inhibit playback from the FPGA's internal buffer
		WRITEL(0, marian->iobase + 0x08);
		WRITEL(0, marian->iobase + 0x0C);
		WRITEL(0, marian->iobase + 0x20);
		WRITEL(0, marian->iobase + 0x24);
		WRITEL(0, marian->iobase + 0x28);
		WRITEL(0, marian->iobase + 0x2C);
		WRITEL(0, marian->iobase + 0x30);
		WRITEL(0, marian->iobase + 0x34);
		WRITEL(0, marian->iobase + 0x38);
		WRITEL(0, marian->iobase + 0x3C);
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

static int snd_marian_mmap(struct snd_pcm_substream *substream, struct vm_area_struct *vma)
{
	struct marian_card *marian = snd_pcm_substream_chip(substream);

	dev_dbg(marian->card->dev,
		"(%d, %016lx)\n", substream->stream, vma->vm_start);
	dev_dbg(marian->card->dev,
		"  substream->runtime.dma_addr = %016lx (%016lx >> %u)\n  substream->runtime.dma_area = %016lx\n  substream->runtime.dma_bytes = %u\n",
		substream->runtime->dma_addr >> PAGE_SHIFT, substream->runtime->dma_addr,
		PAGE_SHIFT, substream->runtime->dma_area, substream->runtime->dma_bytes);
	dev_dbg(marian->card->dev,
		"  vma->vm_start = %016lx\n  vma->vm_end = %016lx\n  vma->vm_page_prot = %016lx\n",
		vma->vm_start, vma->vm_end, vma->vm_page_prot);

	if (remap_pfn_range(vma, vma->vm_start, substream->runtime->dma_addr >> PAGE_SHIFT,
			    vma->vm_end - vma->vm_start, vma->vm_page_prot) < 0) {
		dev_err(marian->card->dev, "remap_pfn_range failed\n");
		return -EIO;
	}

	return 0;
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

static void snd_marian_proc_status(struct snd_info_entry  *entry, struct snd_info_buffer *buffer)
{
	struct marian_card *marian = entry->private_data;

	if (marian->desc->proc_status)
		marian->desc->proc_status(marian, buffer);
	else
		marian_proc_status_generic(marian, buffer);
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
