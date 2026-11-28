/*
CONTENT_AUDIO.C

IMA ADPCM's step and its tables (content_audio.h).
*/

#include "content_audio.h"

const int content_ima_steps[CONTENT_IMA_STEP_COUNT] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767,
};

const int content_ima_changes[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
};

int content_ima_expand(int code, int *value, int *step)
{
	int size = content_ima_steps[*step];
	int difference = size >> 3;

	code &= 15;
	if (code & 1) difference += size >> 2;
	if (code & 2) difference += size >> 1;
	if (code & 4) difference += size;
	if (code & 8) difference = -difference;
	*value += difference;
	if (*value > 32767) *value = 32767;
	if (*value < -32768) *value = -32768;
	*step += content_ima_changes[code];
	if (*step < 0) *step = 0;
	if (*step > 88) *step = 88;
	return *value;
}
