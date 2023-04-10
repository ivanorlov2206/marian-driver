#include "common.h"
#include "a3.h"
#include "generic.h"

//
// ALSA controls
//

//
// RW
//

static int marian_a3_clock_source_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = { "Internal", "Sync Bus", "ADAT Input 1",
				"ADAT Input 2", "ADAT Input 3" };
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 5;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int marian_a3_clock_source_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	switch (marian->clock_source) {
	case 1:
		ucontrol->value.enumerated.item[0] = 0;
		break;
	case 2:
		ucontrol->value.enumerated.item[0] = 1;
		break;
	case 4:
		ucontrol->value.enumerated.item[0] = 2;
		break;
	case 5:
		ucontrol->value.enumerated.item[0] = 3;
		break;
	case 6:
		ucontrol->value.enumerated.item[0] = 4;
		break;
	default:
		dev_dbg(marian->card->dev,
			"marian_a3_clock_source_get: Illegal value for clock_source! (%d)\n",
			marian->clock_source);
		return -1;
	}

	return 0;
}

static int marian_a3_clock_source_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	switch (ucontrol->value.enumerated.item[0]) {
	case CLOCK_SRC_DCO: // DCO
		marian_generic_set_clock_source(marian, 1);
		break;
	case CLOCK_SRC_SYNCBUS: // Sync bus
		marian_generic_set_clock_source(marian, 2);
		break;
	case CLOCK_SRC_ADAT1: // ADAT input 1
		marian_generic_set_clock_source(marian, 4);
		break;
	case CLOCK_SRC_ADAT2: // ADAT input 2
		marian_generic_set_clock_source(marian, 5);
		break;
	case CLOCK_SRC_ADAT3: // ADAT input 3
		marian_generic_set_clock_source(marian, 6);
		break;
	}

	return 0;
}

static int marian_a3_clock_source_create(struct marian_card *marian, char *label)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_a3_clock_source_info,
		.get = marian_a3_clock_source_get,
		.put = marian_a3_clock_source_put,
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

/*
 * Controls:
 *
 * RO:
 *   - Input 1 sync state (no signal, lock, sync)
 *   - Input 1 frequency
 *   - Input 2 sync state (no signal, lock, sync)
 *   - Input 2 frequency
 *   - Input 3 sync state (no signal, lock, sync)
 *   - Input 3 frequency
 *
 * RW:
 *   - Word clock source (Port 1, Port 2, Port 3, Internal, Sync port)
 *   - Speed mode (1, 2, 4FS)
 *   - DCO frequency (1 Hertz)
 *   - DCO frequency (1/1000th)
 *
 */

void marian_a3_create_controls(struct marian_card *marian)
{
	//marian_a3_sync_state_create(marian, "Input 1 Sync", 0);
	//marian_a3_sync_state_create(marian, "Input 2 Sync", 1);
	//marian_a3_sync_state_create(marian, "Input 3 Sync", 2);
	marian_generic_frequency_create(marian, "Input 1 Frequency", 4);
	marian_generic_frequency_create(marian, "Input 2 Frequency", 5);
	marian_generic_frequency_create(marian, "Input 3 Frequency", 6);
	marian_a3_clock_source_create(marian, "Clock Source");
	marian_generic_speedmode_create(marian);
	marian_generic_dco_create(marian);
}

void marian_a3_prepare(struct marian_card *marian)
{
	uint32_t mask = 0x00FFFFFF;

	// arm channels
	WRITEL(mask, marian->iobase + 0x08);
	WRITEL(mask, marian->iobase + 0x0C);

	// unmute inputs
	WRITEL(0x00, marian->iobase + 0x18);
}

int marian_a3_init(struct marian_card *marian)
{
	int err;

	err = marian_generic_init(marian);

	if (err != 0)
		return err;

	// ADAT TX enable
	WRITEL(0x01, marian->iobase + 0x14);

	return 0;
}

void marian_a3_proc_ports(struct marian_card *marian, struct snd_info_buffer *buffer,
			  unsigned int type)
{
	int i;

	for (i = 0; i <= PORTS_COUNT; i++)
		snd_iprintf(buffer, "%d=ADAT p%dch%02d\n", i + 1, i / 8 + 1, i % 8 + 1);
}

void marian_a3_proc_status(struct marian_card *marian, struct snd_info_buffer *buffer)
{
	uint32_t *buf;
	unsigned int i;

	marian_proc_status_generic(marian, buffer);

	buf = (uint32_t *)marian->dmabuf.area;

	snd_iprintf(buffer, "Clock source: ");
	switch (marian->clock_source) {
	case 1:
		snd_iprintf(buffer, "Internal DCO\n");
		break;
	case 2:
		snd_iprintf(buffer, "Sync bus\n");
		break;
	case 4:
		snd_iprintf(buffer, "ADAT Input 1\n");
		break;
	case 5:
		snd_iprintf(buffer, "ADAT Input 2\n");
		break;
	case 6:
		snd_iprintf(buffer, "ADAT Input 3\n");
		break;
	default:
		snd_iprintf(buffer, "UNKNOWN\n");
		break;
	}

	for (i = 1; i <= 3; i++)
		snd_iprintf(buffer, "ADAT port %u input: %u Hz\n",
			    i, marian_measure_freq(marian, 3 + i));

	for (i = 0; i < 512; i++) {
		if (i % 64 == 0)
			snd_iprintf(buffer, "\n%4dK:\t", i);
		else if (i % 8 == 0)
			snd_iprintf(buffer, " ");

		snd_iprintf(buffer, (*buf > 0) ? "X" : "0");
		buf += 256;
	}
}
