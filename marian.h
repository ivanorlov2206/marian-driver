/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SOUND_MARIAN_H

#define __SOUND_MARIAN_H

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

#define PORTS_COUNT 23
#define A3_CLOCK_SRC_CNT	5
#define A3_CLOCK_SRC_DCO	1
#define A3_CLOCK_SRC_SYNCBUS	2
#define A3_CLOCK_SRC_ADAT1	4
#define A3_CLOCK_SRC_ADAT2	5
#define A3_CLOCK_SRC_ADAT3	6
#define A3_INP1_FREQ_CTL_ID	4
#define A3_INP2_FREQ_CTL_ID	5
#define A3_INP3_FREQ_CTL_ID	6

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

#define S8_CLOCK_SRC_CNT	2
#define S8_CLOCK_SRC_DCO	1
#define S8_CLOCK_SRC_SYNCBUS	2

struct marian_card_descriptor;
struct marian_card;

typedef void (*marian_hw_constraints_func)(struct marian_card *marian,
					   struct snd_pcm_substream *substream,
					   struct snd_pcm_hw_params *params);
typedef void (*marian_controls_func)(struct marian_card *marian);
typedef void (*marian_init_func)(struct marian_card *marian);
typedef void (*marian_free_func)(struct marian_card *marian);
typedef void (*marian_prepare_func)(struct marian_card *marian);
typedef void (*marian_init_codec_func)(struct marian_card *marian);
typedef void (*marian_set_speedmode_func)(struct marian_card *marian, unsigned int speedmode);
typedef void (*marian_proc_status_func)(struct marian_card *marian, struct snd_info_buffer *buffer);
typedef void (*marian_proc_ports_func)(struct marian_card *marian, struct snd_info_buffer *buffer,
					unsigned int type);

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

struct m2_specific {
	u8 shadow_40;
	u8 shadow_41;
	u8 shadow_42;
	u8 frame;
};

void marian_generic_init(struct marian_card *marian);
void marian_proc_status_generic(struct marian_card *marian, struct snd_info_buffer *buffer);
void marian_proc_ports_generic(struct marian_card *marian, struct snd_info_buffer *buffer,
			       unsigned int type);
unsigned int marian_measure_freq(struct marian_card *marian, unsigned int source);
int marian_generic_frequency_create(struct marian_card *marian, char *label, u32 idx);
int marian_generic_speedmode_create(struct marian_card *marian);
void marian_generic_dco_create(struct marian_card *marian);

void marian_generic_set_speedmode(struct marian_card *marian, unsigned int speedmode);
void marian_generic_set_clock_source(struct marian_card *marian, u8 source);
void marian_generic_set_dco(struct marian_card *marian, u32 freq, u32 millis);

int marian_spi_transfer(struct marian_card *marian, uint16_t cs, uint16_t bits_write,
			u8 *data_write, uint16_t bits_read, u8 *data_read);

void marian_a3_prepare(struct marian_card *marian);
void marian_a3_init(struct marian_card *marian);
void marian_a3_proc_ports(struct marian_card *marian, struct snd_info_buffer *buffer,
			  unsigned int type);
void marian_a3_proc_status(struct marian_card *marian, struct snd_info_buffer *buffer);
void marian_a3_create_controls(struct marian_card *marian);

void marian_m2_constraints(struct marian_card *marian, struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params);
void marian_m2_create_controls(struct marian_card *marian);
void marian_m2_init(struct marian_card *marian);
void marian_m2_free(struct marian_card *marian);
void marian_m2_init_codec(struct marian_card *marian);
void marian_m2_prepare(struct marian_card *marian);
void marian_m2_proc_status(struct marian_card *marian, struct snd_info_buffer *buffer);
void marian_m2_proc_ports(struct marian_card *marian,
			  struct snd_info_buffer *buffer, unsigned int type);

void marian_m2_set_speedmode(struct marian_card *marian, unsigned int speedmode);

void marian_seraph8_prepare(struct marian_card *marian);
void marian_seraph8_init_codec(struct marian_card *marian);
void marian_seraph8_proc_status(struct marian_card *marian, struct snd_info_buffer *buffer);
void marian_seraph8_create_controls(struct marian_card *marian);

#endif
