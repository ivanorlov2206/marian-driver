#ifndef _SERAPH8_H_

#define _SERAPH8_H_

void marian_seraph8_prepare(struct marian_card *marian);
void marian_seraph8_init_codec(struct marian_card *marian);
void marian_seraph8_proc_status(struct marian_card *marian, struct snd_info_buffer *buffer);
void marian_seraph8_create_controls(struct marian_card *marian);

#endif
