#ifndef _GENERIC_H_

#define _GENERIC_H_

int marian_generic_init(struct marian_card *marian);
void marian_proc_status_generic(struct marian_card *marian, struct snd_info_buffer *buffer);
void marian_proc_ports_generic(struct marian_card *marian, struct snd_info_buffer *buffer,
			       unsigned int type);
unsigned int marian_measure_freq(struct marian_card *marian, unsigned int source);
int marian_generic_frequency_create(struct marian_card *marian, char *label, u32 idx);
int marian_generic_speedmode_create(struct marian_card *marian);
int marian_generic_dco_create(struct marian_card *marian);

void marian_generic_set_speedmode(struct marian_card *marian, unsigned int speedmode);
void marian_generic_set_clock_source(struct marian_card *marian, u8 source);
void marian_generic_set_dco(struct marian_card *marian, u32 freq, u32 millis);

#define SPI_WAIT_FOR_AR(tries)						\
	do {								\
		tries = 10;						\
		while (tries > 0) {					\
			if (readl(marian->iobase + 0x70) == 0x80000000) \
				break;					\
			msleep(1);					\
			tries--;					\
		}							\
	} while (0)

int marian_spi_transfer(struct marian_card *marian, uint16_t cs, uint16_t bits_write,
			u8 *data_write, uint16_t bits_read, u8 *data_read);

#endif
