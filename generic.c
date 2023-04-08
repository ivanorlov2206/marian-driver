#include "common.h"
#include "generic.h"

//
// generic ALSA control functions


/**
 * Measure the frequency of a clock source.
 * The measurement is triggered and the FPGA's ready
 * signal polled (normally takes up to 2ms). The measurement
 * has only a certainty of 10-20Hz, this function rounds it up
 * to the nearest 10Hz step (in 1FS).
 **/
unsigned int marian_measure_freq(struct marian_card *marian, unsigned int source)
{
	uint32_t val;
	int tries = 5;

	WRITEL(source & 0x7, marian->iobase + 0xC8);

	while (tries>0) {
		val = readl(marian->iobase + 0x94);
		if (val & 0x80000000)
			break;

		msleep(1);
		tries--;
	}

	//snd_printk(KERN_INFO "measure_freq(%d) got 0x%08x (%d Hz) after %d tries\n", source, val, 1280000000/((val & 0x3FFFF)+1), 5-tries);

	if (tries>0)
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


int marian_generic_frequency_create(struct marian_card *marian, char *label, uint32_t idx)
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


void marian_generic_set_dco(struct marian_card *marian, unsigned int freq, unsigned int millis)
{
	uint64_t val, v2;
	int64_t detune;

	MDEBUG("marian_generic_set_dco(.., %u, %u)\n", freq, millis);

	val = (freq * 1000 + millis) * marian->speedmode;
	val <<= 36;

	if (marian->detune != 0) {
		// DCO detune active
		// this calculation takes a bit of a shortcut
		// - should be implemented using a logarithmic scale
		detune = marian->detune * 100;
		v2 = val;
		div_u64(v2, 138564);
		detune *= v2;
		//detune *= val/138564;
		val += detune;
	}

	//val /= 80000000000;
	val = div_u64(val, 80000000);
	val = div_u64(val, 1000);

	MDEBUG("  -> 0x%016llx (%llu)\n", val, val);
	WRITEL(val, marian->iobase + 0x88);

	marian->dco = freq;
	marian->dco_millis = millis;
}


static int marian_generic_dco_int_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	if (marian->speedmode == 1) {
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

int marian_generic_dco_int_create(struct marian_card *marian, char *label)
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
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

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


int marian_generic_dco_millis_create(struct marian_card *marian, char *label) {
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
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

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


int marian_generic_dco_detune_create(struct marian_card *marian, char *label)
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


int marian_generic_dco_create(struct marian_card *marian)
{
	marian_generic_dco_int_create(marian, "DCO Freq (Hz)");
	marian_generic_dco_millis_create(marian, "DCO Freq (millis)");
	marian_generic_dco_detune_create(marian, "DCO Detune (cent)");

	return 0;
}


int marian_generic_init(struct marian_card *marian)
{
	MDEBUG("marian_generic_init()\n");

	if (!marian->desc->set_speedmode)
		marian->desc->set_speedmode = marian_generic_set_speedmode;

	// reset DMA engine
	WRITEL(0x00000000, marian->iobase);

	// disable play interrupt
	WRITEL(0x02, marian->iobase + 0xAC);

	marian_generic_set_dco(marian, 48000, 0);

	// init clock mode
	marian_generic_set_speedmode(marian, 1);

	// init internal clock and set it as clock source
	marian_generic_set_clock_source(marian, 1);


	// init SPI clock divider
	WRITEL(0x1F, marian->iobase + 0x74);       /* set up clock divider */

	return 0;
}


/*
static void marian_prepare_generic(struct marian_card *marian)
{
	unsigned int val;

	snd_printk(KERN_INFO "marian_prepare_generic()\n");

	// init DMA engine
	snd_printk(KERN_INFO "  setting DMA ADR to %08x\n", (unsigned int) marian->capture_substream->runtime->dma_addr);
	WRITEL(marian->capture_substream->runtime->dma_addr, marian->iobase + SERAPH_WR_DMA_ADR);
	snd_printk(KERN_INFO "  setting DMA block count to %d\n", 64);
	WRITEL(64, marian->iobase + SERAPH_WR_DMA_BLOCKS);

	val = set_lo_bits(8);
	snd_printk(KERN_INFO "  enabling %d DMA capture channels\n", 8);
	WRITEL(val, marian->iobase + SERAPH_WR_ENABLE_CAPTURE);
	snd_printk(KERN_INFO "  enabling %d DMA playback channels\n", 8);
	WRITEL(val, marian->iobase + SERAPH_WR_ENABLE_PLAYBACK);

}
*/


void marian_proc_status_generic(struct marian_card *marian, struct snd_info_buffer *buffer)
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
	snd_iprintf(buffer, "Clock master : %s\n", (marian->clock_source==1)?"yes":"no");
	snd_iprintf(buffer, "DCO frequency: %d.%d Hz\n", marian->dco, marian->dco_millis);
	snd_iprintf(buffer, "DCO detune   : %d Cent\n", marian->detune);
}



/**
 * Default port name function, outputs the static string
 * port_names of the card descriptor regardless of current
 * speed mode and wether input or output ports are requested.
 **/
void marian_proc_ports_generic(struct marian_card *marian, struct snd_info_buffer *buffer,
				unsigned int type)
{
	snd_iprintf(buffer, marian->desc->port_names);
}




//
// ALSA controls
//

void marian_generic_set_speedmode(struct marian_card *marian, unsigned int speedmode)
{
	snd_printdd(KERN_ERR "marian_generic_set_speedmode(.., %u)\n", speedmode);

	if (speedmode>marian->desc->speedmode_max)
		return;

	switch (speedmode) {
	case 1:
		WRITEL(0x03, marian->iobase + 0x80);
		WRITEL(0x00, marian->iobase + 0x8C);        /* for 48kHz in 1FS mode */
		//WRITEL(0x02, marian->iobase + 0x8C);        /* for 48kHz in 1FS mode */
		marian->speedmode = 1;
		break;
	case 2:
		WRITEL(0x03, marian->iobase + 0x80)
		WRITEL(0x01, marian->iobase + 0x8C);        /* for 96kHz in 2FS mode */
		marian->speedmode = 2;
		break;
	case 4:
		WRITEL(0x03, marian->iobase + 0x80);
		WRITEL(0x00, marian->iobase + 0x8C);        /* for 192kHz in 4FS mode */
		marian->speedmode = 4;
		break;
	}

	marian_generic_set_dco(marian, marian->dco, marian->dco_millis);
}


static int marian_generic_speedmode_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);
	static char *texts[] = { "1FS", "2FS", "4FS" };
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	switch (marian->desc->speedmode_max) {
	case 1:
		uinfo->value.enumerated.items = 1;
		break;
	case 2:
		uinfo->value.enumerated.items = 2;
		break;
	case 4:
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

	if (marian->speedmode == 4)
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
		marian->desc->set_speedmode(marian, 4);

	return 0;
}


int marian_generic_speedmode_create(struct marian_card *marian)
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


void marian_generic_set_clock_source(struct marian_card *marian, uint8_t source)
{
	WRITEL(source, marian->iobase + 0x90);
	marian->clock_source = source;
}


