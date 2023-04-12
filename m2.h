/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _M2_H_

#define _M2_H_

void marian_m2_constraints(struct marian_card *marian, struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params);
void marian_m2_create_controls(struct marian_card *marian);
int marian_m2_init(struct marian_card *marian);
void marian_m2_free(struct marian_card *marian);
void marian_m2_init_codec(struct marian_card *marian);
void marian_m2_prepare(struct marian_card *marian);
void marian_m2_proc_status(struct marian_card *marian, struct snd_info_buffer *buffer);
void marian_m2_proc_ports(struct marian_card *marian,
			  struct snd_info_buffer *buffer, unsigned int type);

void marian_m2_set_speedmode(struct marian_card *marian, unsigned int speedmode);

#define M2_CLOCK_SRC_DCO	1
#define M2_CLOCK_SRC_SYNCBUS	2
#define M2_CLOCK_SRC_MADI1	4
#define M2_CLOCK_SRC_MADI2	5

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

#endif
