/*
TOML_FUZZ.C

libFuzzer's entry for config.toml's parser (port/third_party/tomlc17, as
port_config.c calls it: the text and its length), what a config file
handed around or edited by hand may hold (tools/test_host_fuzz.py builds
it, target toml). The text comes in an allocation of exactly its size.
*/

#include "tomlc17.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	toml_result_t result;
	char *text;

	if (size > 65536)
		return 0;
	text = (char *)malloc(size ? size : 1);
	if (!text)
		return 0;
	memcpy(text, data, size);
	result = toml_parse(text, (int)size);
	if (result.ok)
	{
		toml_datum_t value = toml_seek(result.toptab, "network.browser_url");

		(void)value;
	}
	toml_free(result);
	free(text);
	return 0;
}
