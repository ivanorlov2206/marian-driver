
#include "common.h"


#define SPI_WAIT_FOR_AR(tries) { \
					tries = 10; \
					while (tries>0) { \
						if (readl(marian->iobase + 0x70) == 0x80000000) \
							break; \
						msleep(1); \
						tries--; \
					} \
				}


int marian_spi_transfer(struct marian_card* marian, uint16_t cs, uint16_t bits_write,
			uint8_t* data_write, uint16_t bits_read, uint8_t* data_read)
{
	int tries = 10;
	uint32_t buf = 0;
	unsigned int i;

	MDEBUG("spi_transfer(.., 0x%04x, %u, [%02x, %02x], %u, ..)\n", cs, bits_write, data_write[0], data_write[1], bits_read);

	SPI_WAIT_FOR_AR(tries);
	if (tries == 0) {
		MDEBUG("marian_spi_transfer: Resetting SPI bus\n");
		writel(0x1234, marian->iobase + 0x70);
	}

	//snd_printk(KERN_INFO "marian_spi_transfer: Bus ready after %d tries.\n", 11-tries);


	writel(cs, marian->iobase + 0x60);         /* chip select register */
	writel(bits_write, marian->iobase + 0x64); /* number of bits to write */
	writel(bits_read, marian->iobase + 0x68);  /* number of bits to read */

	if (bits_write <= 32) {
		if (bits_write <= 8)
			buf = data_write[0]<<(32-bits_write);
		else if (bits_write<=16)
			buf = data_write[0]<<24 | data_write[1]<<(32-bits_write);

		//snd_printk(KERN_INFO "marian_spi_transfer: Writing %d bits, 0x%08x\n", bits_write, buf);

		writel(buf, marian->iobase + 0x6C); /* write data left aligned */

	}
	if (bits_read > 0) {
		if (bits_read <= 32) {
			tries = 10;
			SPI_WAIT_FOR_AR(tries);
			if (tries == 0) {
				snd_printk(KERN_INFO "marian_spi_transfer: bus didn't signal AR\n");
				return -1;
			}

			//snd_printk(KERN_INFO "marian_spi_transfer: Bus ready for reading after %d tries.\n", 11-tries);

			//snd_printk(KERN_INFO "marian_spi_transfer: %u bits read\n", readl(marian->iobase + 0x68));

			buf = readl(marian->iobase + 0x74);
			//snd_printk(KERN_INFO "marian_spi_transfer: Read 0x%08x\n", buf);

			buf <<= 32 - bits_read;
			i = 0;

			while (bits_read > 0) {
				data_read[i++] = (buf >> 24) & 0xFF;
				buf <<= 8;
				bits_read -= 8;
			}


		}
	}

	return 0;
}
