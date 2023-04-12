// SPDX-License-Identifier: GPL-2.0-only
#include "common.h"
#include "generic.h"
#include "m2.h"

struct m2_specific {
	u8 shadow_40;
	u8 shadow_41;
	u8 shadow_42;
	u8 frame;
};

static u8 marian_m2_spi_read(struct marian_card *marian, u8 adr);
static int marian_m2_spi_write(struct marian_card *marian, u8 adr, u8 val);
static int marian_m2_sync_state_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo);
static int marian_m2_sync_state_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol);
static int marian_m2_sync_state_create(struct marian_card *marian, char *label, u32 idx);
static int marian_m2_channel_mode_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo);
static int marian_m2_input_channel_mode_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol);
static int marian_m2_input_channel_mode_create(struct marian_card *marian,
					       char *label, u32 idx);
static int marian_m2_frame_mode_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo);
static int marian_m2_input_frame_mode_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol);
static int marian_m2_input_frame_mode_create(struct marian_card *marian, char *label, u32 idx);
static int marian_m2_output_channel_mode_get(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol);
static int marian_m2_output_channel_mode_put(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol);

static int marian_m2_output_channel_mode_create(struct marian_card *marian,
						char *label, u32 idx);
static int marian_m2_output_frame_mode_get(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol);
static int marian_m2_output_frame_mode_put(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol);
static int marian_m2_output_frame_mode_create(struct marian_card *marian,
					      char *label, u32 idx);

static void marian_m2_set_pll(struct marian_card *marian, u8 state);
static void marian_m2_enable_tx(struct marian_card *marian, u8 state);
static void marian_m2_set_float(struct marian_card *marian, u8 state);
static void marian_m2_set_endianness(struct marian_card *marian, u8 state);
static void marian_m2_set_bit_order(struct marian_card *marian, u8 state);
static void marian_m2_set_port_mode(struct marian_card *marian, unsigned int port, u8 state);
static u8 marian_m2_get_port_mode(struct marian_card *marian, unsigned int port);
static void marian_m2_write_port_frame(struct marian_card *marian);
static void marian_m2_set_port_frame(struct marian_card *marian, unsigned int port, u8 state);
static u8 marian_m2_get_port_frame(struct marian_card *marian, unsigned int port);

static u8 marian_m2_spi_read(struct marian_card *marian, u8 adr)
{
	u8 buf_in;

	adr = adr & 0x7F;

	if (marian_spi_transfer(marian, 0x02, 8, &adr, 8, &buf_in) == 0)
		return buf_in;

	return 0;
}

static int marian_m2_spi_write(struct marian_card *marian, u8 adr, u8 val)
{
	u8 buf_out[2];

	buf_out[0] = 0x80 | adr;
	buf_out[1] = val;

	return marian_spi_transfer(marian, 0x02, 16, (u8 *)&buf_out, 0, NULL);
}

// RO controls

static int marian_m2_sync_state_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = { "No Signal", "Lock", "Sync" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int marian_m2_sync_state_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);
	u8 v = marian_m2_spi_read(marian, 0x00);

	v = (v >> (kcontrol->private_value * 2)) & 0x3;
	if (v == 3)
		v--;

	ucontrol->value.enumerated.item[0] = v;

	return 0;
}

static int marian_m2_sync_state_create(struct marian_card *marian, char *label, u32 idx)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.private_value = idx,
		.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_m2_sync_state_info,
		.get = marian_m2_sync_state_get
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static int marian_m2_channel_mode_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = { "56ch", "64ch" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int marian_m2_input_channel_mode_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);
	u8 v = marian_m2_spi_read(marian, 0x01);

	v = (v >> (kcontrol->private_value * 2)) & 0x1;
	ucontrol->value.enumerated.item[0] = v;

	return 0;
}

static int marian_m2_input_channel_mode_create(struct marian_card *marian,
					       char *label, u32 idx)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.private_value = idx,
		.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_m2_channel_mode_info,
		.get = marian_m2_input_channel_mode_get
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static int marian_m2_frame_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = { "48kHz", "96kHz" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int marian_m2_input_frame_mode_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);
	u8 v = marian_m2_spi_read(marian, 0x01);

	v = (v >> ((kcontrol->private_value * 2) + 1)) & 0x1;
	ucontrol->value.enumerated.item[0] = v;

	return 0;
}

static int marian_m2_input_frame_mode_create(struct marian_card *marian, char *label, u32 idx)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.private_value = idx,
		.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_m2_frame_mode_info,
		.get = marian_m2_input_frame_mode_get
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

// RW controls

static int marian_m2_output_channel_mode_get(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = marian_m2_get_port_mode(marian,
								     kcontrol->private_value);

	return 0;
}

static int marian_m2_output_channel_mode_put(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	marian_m2_set_port_mode(marian, kcontrol->private_value,
				ucontrol->value.enumerated.item[0]);

	return 0;
}

static int marian_m2_output_channel_mode_create(struct marian_card *marian,
						char *label, u32 idx)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.private_value = idx,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_m2_channel_mode_info,
		.get = marian_m2_output_channel_mode_get,
		.put = marian_m2_output_channel_mode_put
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static int marian_m2_output_frame_mode_info(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}

static int marian_m2_output_frame_mode_get(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	ucontrol->value.enumerated.item[0] = (spec->frame >> kcontrol->private_value) & 1;

	return 0;
}

static int marian_m2_output_frame_mode_put(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	dev_dbg(marian->card->dev, "%d -> %d\n",
		kcontrol->private_value, ucontrol->value.enumerated.item[0]);

	marian_m2_set_port_frame(marian, kcontrol->private_value,
				 ucontrol->value.enumerated.item[0]);

	return 0;
}

static int marian_m2_output_frame_mode_create(struct marian_card *marian, char *label, u32 idx)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.private_value = idx,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_m2_output_frame_mode_info,
		.get = marian_m2_output_frame_mode_get,
		.put = marian_m2_output_frame_mode_put,
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

static int marian_m2_clock_source_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = {"Internal", "Sync Bus",
					      "Input Port 1", "Input Port 2"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int marian_m2_clock_source_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	switch (marian->clock_source) {
	case M2_CLOCK_SRC_DCO:
		ucontrol->value.enumerated.item[0] = CLOCK_SRC_INTERNAL;
		break;
	case M2_CLOCK_SRC_SYNCBUS:
		ucontrol->value.enumerated.item[0] = CLOCK_SRC_SYNCBUS;
		break;
	case M2_CLOCK_SRC_MADI1:
		ucontrol->value.enumerated.item[0] = CLOCK_SRC_INP1;
		break;
	case M2_CLOCK_SRC_MADI2:
		ucontrol->value.enumerated.item[0] = CLOCK_SRC_INP2;
		break;
	default:
		dev_dbg(marian->card->dev,
			"Illegal value for clock_source! (%d)\n",
			marian->clock_source);
		return -1;
	}

	return 0;
}

static int marian_m2_clock_source_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct marian_card *marian = snd_kcontrol_chip(kcontrol);

	switch (ucontrol->value.enumerated.item[0]) {
	case CLOCK_SRC_INTERNAL: // DCO
		marian_generic_set_clock_source(marian, M2_CLOCK_SRC_DCO);
		break;
	case CLOCK_SRC_SYNCBUS: // Sync bus
		marian_generic_set_clock_source(marian, M2_CLOCK_SRC_SYNCBUS);
		break;
	case CLOCK_SRC_INP1: // MADI port 1
		marian_generic_set_clock_source(marian, M2_CLOCK_SRC_MADI1);
		break;
	case CLOCK_SRC_INP2: // MADI port 2
		marian_generic_set_clock_source(marian, M2_CLOCK_SRC_MADI2);
		break;
	}

	return 0;
}

static int marian_m2_clock_source_create(struct marian_card *marian, char *label)
{
	struct snd_kcontrol_new c = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = label,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = marian_m2_clock_source_info,
		.get = marian_m2_clock_source_get,
		.put = marian_m2_clock_source_put,
	};

	return snd_ctl_add(marian->card, snd_ctl_new1(&c, marian));
}

/*
 * Controls:
 *
 * RO:
 *   - Input 1 sync state (no signal, lock, sync)
 *   - Input 1 channel mode (56/64ch)
 *   - Input 1 frame mode (48/96kHz)
 *   - Input 1 frequency
 *   - Input 2 sync state (no signal, lock, sync)
 *   - Input 2 channel mode (56/64ch)
 *   - Input 2 frame mode (48/96kHz)
 *   - Input 2 frequency
 *
 * RW:
 *   - Output 1 channel mode (56/64ch)
 *   - Output 1 frame mode (48/96kHz)
 *   - Output 2 channel mode (56/64ch)
 *   - Output 2 frame mode (48/96kHz)
 *   - Word clock source (Port 1, Port 2, Internal, Sync port, WCK input)
 *   - Speed mode (1, 2, 4FS)
 *   - DCO frequency (1 Hertz)
 *   - DCO frequency (1/1000th)
 */
void marian_m2_create_controls(struct marian_card *marian)
{
	marian_m2_sync_state_create(marian, "Input 1 Sync", 0);
	marian_m2_sync_state_create(marian, "Input 2 Sync", 1);
	marian_m2_input_channel_mode_create(marian, "Input 1 Channel Mode", 0);
	marian_m2_input_channel_mode_create(marian, "Input 2 Channel Mode", 1);
	marian_m2_input_frame_mode_create(marian, "Input 1 Frame Mode", 0);
	marian_m2_input_frame_mode_create(marian, "Input 2 Frame Mode", 1);
	marian_generic_frequency_create(marian, "Input 1 Frequency", 4);
	marian_generic_frequency_create(marian, "Input 2 Frequency", 5);
	marian_m2_output_channel_mode_create(marian, "Output 1 Channel Mode", 0);
	marian_m2_output_channel_mode_create(marian, "Output 2 Channel Mode", 1);
	marian_m2_output_frame_mode_create(marian, "Output 1 96kHz Frame", 0);
	marian_m2_output_frame_mode_create(marian, "Output 2 96kHz Frame", 1);
	marian_m2_clock_source_create(marian, "Clock Source");
	marian_generic_speedmode_create(marian);
	marian_generic_dco_create(marian);
}

/*
 * Enable (state=1) or disable (state=0) the external PLL for the
 * MADI FPGA.
 */
static void marian_m2_set_pll(struct marian_card *marian, u8 state)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	spec->shadow_40 = (spec->shadow_40 & ~(1 << M2_PLL)) | state << M2_PLL;
	marian_m2_spi_write(marian, 0x40, spec->shadow_40);
}

static void marian_m2_enable_tx(struct marian_card *marian, u8 state)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	spec->shadow_41 = (spec->shadow_41 & ~(1 << M2_TX_ENABLE)) | state << M2_TX_ENABLE;
	marian_m2_spi_write(marian, 0x41, spec->shadow_41);
}

// @param state 0=int, 1=float
static void marian_m2_set_float(struct marian_card *marian, u8 state)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	spec->shadow_41 = (spec->shadow_41 & ~(1 << M2_INT_FLOAT)) | state << M2_INT_FLOAT;
	marian_m2_spi_write(marian, 0x41, spec->shadow_41);
}

// @param state 0=big endian, 1=little endian
static void marian_m2_set_endianness(struct marian_card *marian, u8 state)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	spec->shadow_41 = (spec->shadow_41 & ~(1 << M2_ENDIANNESS)) | state << M2_ENDIANNESS;
	marian_m2_spi_write(marian, 0x41, spec->shadow_41);
}

// @param state 0=MSB first, 1=LSB first
static void marian_m2_set_bit_order(struct marian_card *marian, u8 state)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	spec->shadow_41 = (spec->shadow_41 & ~(1 << M2_BIT_ORDER)) | state << M2_BIT_ORDER;
	marian_m2_spi_write(marian, 0x41, spec->shadow_41);
}

static void marian_m2_set_port_mode(struct marian_card *marian, unsigned int port, u8 state)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	if (port)
		spec->shadow_42 = (spec->shadow_42 & ~(1 << M2_PORT2_MODE))
				| state << M2_PORT2_MODE;
	else
		spec->shadow_42 = (spec->shadow_42 & ~(1 << M2_PORT1_MODE))
				| state << M2_PORT1_MODE;

	marian_m2_spi_write(marian, 0x42, spec->shadow_42);
}

static u8 marian_m2_get_port_mode(struct marian_card *marian, unsigned int port)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	if (port)
		return (spec->shadow_42 >> M2_PORT2_MODE) & 1;
	else
		return (spec->shadow_42 >> M2_PORT1_MODE) & 1;
}

static void marian_m2_write_port_frame(struct marian_card *marian)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	spec->shadow_42 = spec->shadow_42 & ~((1 << M2_PORT1_FRAME) | (1 << M2_PORT2_FRAME));

	if (marian->speedmode == 2) {
		// If we are in FS2, set 96kHz mode where enabled
		if (spec->frame & 1)
			spec->shadow_42 |= 1 << M2_PORT1_FRAME;
		if (spec->frame & 2)
			spec->shadow_42 |= 1 << M2_PORT2_FRAME;
	}

	marian_m2_spi_write(marian, 0x42, spec->shadow_42);
}

static void marian_m2_set_port_frame(struct marian_card *marian, unsigned int port, u8 state)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	spec->frame = (spec->frame & ~(1 << port)) | (state << port);

	marian_m2_write_port_frame(marian);
}

static u8 marian_m2_get_port_frame(struct marian_card *marian, unsigned int port)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;

	if (port)
		return (spec->shadow_42 >> M2_PORT2_FRAME) & 1;
	else
		return (spec->shadow_42 >> M2_PORT1_FRAME) & 1;
}

void marian_m2_set_speedmode(struct marian_card *marian, unsigned int speedmode)
{
	marian_generic_set_speedmode(marian, speedmode);
	marian_m2_write_port_frame(marian);
}

int marian_m2_init(struct marian_card *marian)
{
	int err;
	struct m2_specific *spec;

	err = marian_generic_init(marian);

	if (err != 0)
		return err;

	spec = kzalloc(sizeof(struct m2_specific), GFP_KERNEL);
	if (!spec) {
		dev_dbg(marian->card->dev,
			"Cannot allocate card specific structure\n");
		return -ENOMEM;
	}

	spec->shadow_40 = 0x00;
	spec->shadow_41 = (1 << M2_TX_ENABLE);
	spec->shadow_42 = (1 << M2_PORT1_MODE) | (1 << M2_PORT2_MODE);

	marian_m2_spi_write(marian, 0x40, spec->shadow_40);
	marian_m2_spi_write(marian, 0x41, spec->shadow_41);
	marian_m2_spi_write(marian, 0x42, spec->shadow_42);

	marian->card_specific = spec;

	return 0;
}

void marian_m2_free(struct marian_card *marian)
{
	kfree(marian->card_specific);
}

void marian_m2_init_codec(struct marian_card *marian)
{
}

void marian_m2_prepare(struct marian_card *marian)
{
	u32 mask = 0xFFFFFFFF;

	writel(mask, marian->iobase + 0x20);
	writel(mask, marian->iobase + 0x24);
	writel(mask, marian->iobase + 0x28);
	writel(mask, marian->iobase + 0x2C);
	writel(mask, marian->iobase + 0x30);
	writel(mask, marian->iobase + 0x34);
	writel(mask, marian->iobase + 0x38);
	writel(mask, marian->iobase + 0x3C);
}

void marian_m2_proc_status(struct marian_card *marian, struct snd_info_buffer *buffer)
{
	struct m2_specific *spec = (struct m2_specific *)marian->card_specific;
	u8 v1, v2;
#ifdef DEBUG
	u32 *buf;
	unsigned int i;
#endif

	marian_proc_status_generic(marian, buffer);

	snd_iprintf(buffer, "\n*** MADI FPGA registers\n");
	snd_iprintf(buffer, "M2 MADI 00h: %02x\n", marian_m2_spi_read(marian, 0x00));
	snd_iprintf(buffer, "M2 MADI 01h: %02x\n", marian_m2_spi_read(marian, 0x01));
	snd_iprintf(buffer, "M2 MADI 02h: %02x\n", marian_m2_spi_read(marian, 0x02));
	snd_iprintf(buffer, "M2 MADI 40h: %02x\n", spec->shadow_40);
	snd_iprintf(buffer, "M2 MADI 41h: %02x\n", spec->shadow_41);
	snd_iprintf(buffer, "M2 MADI 42h: %02x\n", spec->shadow_42);

	snd_iprintf(buffer, "\n*** MADI FPGA status\n");
	snd_iprintf(buffer, "MADI FPGA firmware: 0x%02x\n", marian_m2_spi_read(marian, 0x02));

	snd_iprintf(buffer, "Clock source: ");
	switch (marian->clock_source) {
	case 1:
		snd_iprintf(buffer, "Internal DCO\n");
		break;
	case 2:
		snd_iprintf(buffer, "Sync bus\n");
		break;
	case 4:
		snd_iprintf(buffer, "MADI Input 1\n");
		break;
	case 5:
		snd_iprintf(buffer, "MADI Input 2\n");
		break;
	default:
		snd_iprintf(buffer, "UNKNOWN\n");
		break;
	}

	snd_iprintf(buffer, "Sample format: %s, %s Endian, %s first\n",
		    (spec->shadow_41 & (1 << M2_INT_FLOAT)) ? "Float" : "Integer",
		    (spec->shadow_41 & (1 << M2_ENDIANNESS)) ? "Little" : "Big",
		    (spec->shadow_41 & (1 << M2_BIT_ORDER)) ? "LSB" : "MSB");

	v1 = marian_m2_spi_read(marian, 0x00);
	v2 = marian_m2_spi_read(marian, 0x01);

	snd_iprintf(buffer, "MADI port 1 input: ");
	if (!(v1 & 0x03))
		snd_iprintf(buffer, "No signal\n");
	else
		snd_iprintf(buffer, "%s, %dch, %dkHz frame, %u Hz\n",
			    (v1 & 0x02) ? "sync" : "lock", (v2 & 0x01) ? 64 : 56,
			    (v2 & 0x02) ? 96 : 48, marian_measure_freq(marian, 4));

	snd_iprintf(buffer, "MADI port 2 input: ");
	if (!(v1 & 0x0C))
		snd_iprintf(buffer, "No signal\n");
	else
		snd_iprintf(buffer, "%s, %dch, %dkHz frame, %u Hz\n",
			    (v1 & 0x08) ? "sync" : "lock",
			    (v2 & 0x04) ? 64 : 56, (v2 & 0x08) ? 96 : 48,
			    marian_measure_freq(marian, 5));

#ifdef DEBUG
	buf = marian->dmabuf.area;

	for (i = 0; i < 2048; i++) {
		if (i % 64 == 0)
			snd_iprintf(buffer, "\n% 4dK:\t", i);
		else if (i % 8 == 0)
			snd_iprintf(buffer, " ");

		snd_iprintf(buffer, (*buf > 0) ? "X" : " ");
		buf += 256;
	}
#endif
}

void marian_m2_proc_ports(struct marian_card *marian,
			  struct snd_info_buffer *buffer, unsigned int type)
{
	int i;

	for (i = 0; i < 128; i++)
		snd_iprintf(buffer, "%d=MADI p%dch%02d\n", i + 1, i / 64 + 1, i % 64 + 1);
}

void marian_m2_constraints(struct marian_card *marian, struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params)
{
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_FLOAT_BE:
		marian_m2_set_float(marian, 1);
		marian_m2_set_endianness(marian, 0);
		break;
	case SNDRV_PCM_FORMAT_FLOAT_LE:
		marian_m2_set_float(marian, 1);
		marian_m2_set_endianness(marian, 1);
		break;
	case SNDRV_PCM_FORMAT_S32_BE:
		marian_m2_set_float(marian, 0);
		marian_m2_set_endianness(marian, 0);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		marian_m2_set_float(marian, 0);
		marian_m2_set_endianness(marian, 1);
		break;
	}
}
