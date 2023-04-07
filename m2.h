#ifndef _M2_H_

#define _M2_H_

void marian_m2_constraints(struct marian_card*, struct snd_pcm_substream*, struct snd_pcm_hw_params*);
void marian_m2_create_controls(struct marian_card*);
int marian_m2_init(struct marian_card*);
void marian_m2_free(struct marian_card*);
void marian_m2_init_codec(struct marian_card*);
void marian_m2_prepare(struct marian_card*);
void marian_m2_proc_status(struct marian_card*, struct snd_info_buffer*);
void marian_m2_proc_ports(struct marian_card* marian, 
													struct snd_info_buffer *buffer, 
													unsigned int type);

void marian_m2_set_speedmode(struct marian_card* marian, unsigned int speedmode);

#endif
