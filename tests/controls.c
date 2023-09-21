#include <stdio.h>
#include <alsa/asoundlib.h>

#define CLOCK_SRC_ID 12

int main(void)
{
	int err, count, i;
	snd_ctl_t *ctl;
	snd_ctl_elem_list_t *list;
	const char *name;
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_value_t *clock_src_val;

	err = snd_ctl_open(&ctl, "hw:CARD=M2", SND_CTL_NONBLOCK);

	if (err) {
		printf("Failed to open control: %s\n", snd_strerror(err));
		return -1;
	}

	snd_ctl_elem_list_malloc(&list);

	snd_ctl_elem_list(ctl, list);
	count = snd_ctl_elem_list_get_count(list);

	snd_ctl_elem_list_alloc_space(list, count);
	snd_ctl_elem_list(ctl, list);

	for (i = 0; i < count; i++) {
		name = snd_ctl_elem_list_get_name(list, i);
		printf("%d Control: %s\n", i, name);
	}

	snd_ctl_elem_id_malloc(&id);

	snd_ctl_elem_list_get_id(list, CLOCK_SRC_ID, id);

	snd_ctl_elem_value_malloc(&clock_src_val);

	snd_ctl_elem_value_set_id(clock_src_val, id);
	err = snd_ctl_elem_read(ctl, clock_src_val);
	if (err) {
		printf("Error during reading: %s\n", snd_strerror(err));
		goto out;
	}
	unsigned int clock_source = snd_ctl_elem_value_get_enumerated(clock_src_val, CLOCK_SRC_ID);
	printf("Clock source: %u\n", clock_source);

	snd_ctl_elem_value_set_enumerated(clock_src_val, CLOCK_SRC_ID, )

out:
	snd_ctl_elem_value_free(clock_src_val);
	snd_ctl_elem_id_free(id);
	snd_ctl_elem_list_free_space(list);
	snd_ctl_elem_list_free(list);
	return 0;
}
