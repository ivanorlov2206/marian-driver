#ifndef _SPI_H_

#define _SPI_H_

int marian_spi_transfer(struct marian_card* marian, uint16_t cs, uint16_t bits_write, uint8_t* data_write, uint16_t bits_read, uint8_t* data_read);


#endif
