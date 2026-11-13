/*
DELTA_PROFILE_FUZZ.C

libFuzzer's entry for the readers of the site's answers about Delta
profiles (delta_profile.c): each input, ended where it ends (as the
browser's buffer ends an answer), given to every one of them.
*/

#include "../src/delta_profile.h"
#include "../src/social.h"

#include <stdlib.h>
#include <string.h>

static void found(void *context, const char *id, const struct delta_profile_card *card)
{
	(void)context;
	(void)id;
	(void)card;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	static struct delta_profile profile;
	static struct social_state state;
	char *text = malloc(size + 1);
	char small[64];

	if (!text)
		return 0;
	memcpy(text, data, size);
	text[size] = 0;
	memset(&profile, 0, sizeof(profile));
	delta_profile_parse(text, &profile);
	delta_profile_parse_privacy(text, &profile);
	delta_profile_parse_clan(text, &profile);
	delta_profile_parse_cards(text, found, NULL);
	memset(&state, 0, sizeof(state));
	delta_profile_parse_social(text, &state);
	delta_profile_parse_party(text, &state);
	delta_profile_parse_text(text, "reason", small, (int)sizeof(small));
	delta_profile_parse_join(text, small, (int)sizeof(small));
	free(text);
	return 0;
}
