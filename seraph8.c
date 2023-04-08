#include "common.h"
#include "seraph8.h"
#include "spi.h"
#include "generic.h"



//
// ALSA controls
//


//
// RW
//


static int marian_seraph8_clock_source_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = { "Internal", "Sync Bus" };
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}


static int marian_seraph8_clock_source_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	switch (marian->clock_source) {
	case 1:
		ucontrol->value.enumerated.item[0] = 0;
		break;
	case 2:
		ucontrol->value.enumerated.item[0] = 1;
		break;
	default:
		snd_printk(KERN_INFO "marian_seraph8_clock_source_get: Illegal value for clock_source! (%d)\n", marian->clock_source);
		return -1;
	}

	return 0;
}


static int marian_seraph8_clock_source_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	switch(ucontrol->value.enumerated.item[0]) {
	case 0: // DCO
		marian_generic_set_clock_source(marian, 1);
		break;
	case 1: // Sync bus
		marian_generic_set_clock_source(marian, 2);
		break;
	}

	return 0;
}


static int marian_seraph8_clock_source_create(struct marian_card *marian, char *label)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_seraph8_clock_source_info,
		.get = marian_seraph8_clock_source_get,
		.put = marian_seraph8_clock_source_put,
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}


void marian_seraph8_create_controls(struct marian_card *marian)
{
	marian_seraph8_clock_source_create(marian, "Clock Source");
	marian_generic_speedmode_create(marian);
	marian_generic_dco_create(marian);
}


void marian_seraph8_prepare(struct marian_card *marian)
{
	// MARIAN-AH:
  // uint32_t mask = 0xFF000000;
  // Transfer enable Bits for all Analog Channels
  uint32_t mask = 0x000000FF;

	snd_printdd(KERN_INFO "marian_seraph8_prepare()\n");

	// arm channels

	WRITEL(mask, marian->iobase + 0x08);
	WRITEL(mask, marian->iobase + 0x0C);

}


void marian_seraph8_init_codec(struct marian_card *marian)
{
	uint8_t buf_out[2];

	snd_printdd(KERN_ERR "marian_init_codec_seraph8()\n");

	// hold codecs reset line
	WRITEL(0x00, marian->iobase + 0x14);

	// init codec clock divider (128FS)
	WRITEL(0x02, marian->iobase + 0x7C);

	// release codecs reset line
	WRITEL(0x01, marian->iobase + 0x14);

	// enable all codecs
	WRITEL(0x0F, marian->iobase + 0x14);

	// initialize codecs via SPI

	buf_out[0] = 0xA1;
	buf_out[1] = 0x03;

	marian_spi_transfer(marian, 0x1E, 16, (uint8_t*) &buf_out, 0, NULL);

	buf_out[0] = 0xA2;
	buf_out[1] = 0x4D;

	marian_spi_transfer(marian, 0x1E, 16, (uint8_t*) &buf_out, 0, NULL);

	// switch input mute off
	WRITEL(0x0, marian->iobase + 0x18);
}


void marian_seraph8_proc_status(struct marian_card *marian, struct snd_info_buffer *buffer)
{
	//struct m2_specific *spec = (struct m2_specific*) marian->card_specific;
	uint8_t v1, v2;
	uint32_t *buf;
	unsigned int i;

	marian_proc_status_generic(marian, buffer);

	buf = (uint32_t *) marian->dmabuf.area;

	for (i = 0; i < 512; i++) {
		if (i % 64 == 0)
			snd_iprintf(buffer, "\n% 4dK:\t", i);
		else if (i % 8 == 0)
			snd_iprintf(buffer, " ");

		snd_iprintf(buffer, (*buf > 0) ? "X" : "0");
		buf += 256;
	}
}
