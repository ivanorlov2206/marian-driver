#ifndef _A3_H_

#define _A3_H_

void marian_a3_prepare(struct marian_card *marian);
int marian_a3_init(struct marian_card *marian);
void marian_a3_proc_ports(struct marian_card *marian, struct snd_info_buffer *buffer,
			unsigned int type);
void marian_a3_proc_status(struct marian_card *marian, struct snd_info_buffer *buffer);
void marian_a3_create_controls(struct marian_card *marian);

#define PORTS_COUNT 23

#define CLOCK_SRC_DCO 0
#define CLOCK_SRC_SYNCBUS 1
#define CLOCK_SRC_ADAT1 2
#define CLOCK_SRC_ADAT2 3
#define CLOCK_SRC_ADAT3 4

#endif
