#ifndef _COMMON_H_

#define _COMMON_H_

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/info.h>

struct marian_card;
struct marian_card_descriptor;

typedef void (*marian_hw_constraints_func)(struct marian_card *marian,
		struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params);
typedef void (*marian_controls_func)(struct marian_card *marian);
typedef int (*marian_init_func)(struct marian_card *marian);
typedef void (*marian_free_func)(struct marian_card *marian);
typedef void (*marian_prepare_func)(struct marian_card *marian);
typedef void (*marian_init_codec_func)(struct marian_card *marian);
typedef void (*marian_set_speedmode_func)(struct marian_card *marian, unsigned int speedmode);
typedef void (*marian_proc_status_func)(struct marian_card *marian, struct snd_info_buffer *buffer);
typedef void (*marian_proc_ports_func)(struct marian_card *marian, struct snd_info_buffer *buffer,
					unsigned int type);

#define WRITEL(val, adr) { \
			snd_printdd(KERN_DEBUG "writel(%02x, %08x) [%s:%u]\n", val, \
				adr, __FILE__, __LINE__); \
				writel(val, adr); \
			}

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

	marian_hw_constraints_func hw_constraints_func;
	/* custom function to set up ALSA controls */
	marian_controls_func create_controls;
	/* init is called after probing the card */
	marian_init_func init_card;
	marian_free_func free_card;
	/* prepare is called when ALSA is opening the card */
	marian_prepare_func prepare;
	marian_init_codec_func init_codec;
	marian_set_speedmode_func set_speedmode;
	marian_proc_status_func proc_status;
	marian_proc_ports_func proc_ports;

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

#define SERAPH_RD_IRQ_STATUS      0x00
#define SERAPH_RD_HWPOINTER       0x8C

#define SERAPH_WR_DMA_ADR         0x04
#define SERAPH_WR_ENABLE_CAPTURE  0x08
#define SERAPH_WR_ENABLE_PLAYBACK 0x0C
#define SERAPH_WR_DMA_BLOCKS      0x10

#define SERAPH_WR_DMA_ENABLE      0x84
#define SERAPH_WR_IE_ENABLE       0xAC

#define PCI_VENDOR_ID_MARIAN            0x1382
#define PCI_DEVICE_ID_MARIAN_SERAPH_A3  0x4630
#define PCI_DEVICE_ID_MARIAN_C_BOX      0x4640
#define PCI_DEVICE_ID_MARIAN_SERAPH_AD2 0x4720
#define PCI_DEVICE_ID_MARIAN_SERAPH_D4  0x4840
#define PCI_DEVICE_ID_MARIAN_SERAPH_D8  0x4880
#define PCI_DEVICE_ID_MARIAN_SERAPH_8   0x4980
#define PCI_DEVICE_ID_MARIAN_SERAPH_M2  0x5020

#define RATE_SLOW 54000
#define RATE_NORMAL 108000

#define SPEEDMODE_SLOW 1
#define SPEEDMODE_NORMAL 2
#define SPEEDMODE_FAST 4

#define MARIAN_PORTS_TYPE_INPUT 0
#define MARIAN_PORTS_TYPE_OUTPUT 1

#define ERR_DEAD_WRITE BIT(0)
#define ERR_DEAD_READ BIT(1)
#define ERR_DATA_LOST BIT(2)
#define ERR_PAGE_CONF BIT(3)
#define ERR_INT_PLAY BIT(10)
#define ERR_INT_REC BIT(13)

#define STATUS_ST_READY BIT(4)
#define STATUS_INT_PLAY BIT(8)
#define STATUS_INT_PPLAY BIT(9)
#define STATUS_INT_REC BIT(11)
#define STATUS_INT_PREC BIT(12)
#define STATUS_INT_PREP BIT(14)

#endif
