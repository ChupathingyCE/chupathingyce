/*
IOS_PLATFORM.C
iOS-only parts of the platform layer, compiled with the host's own ABI.
*/

/* sdl_platform.c sets the swap interval through macOS's CGL; iOS presents
through Core Animation, which paces itself */
void macos_set_swap_interval(int interval)
{
	(void)interval;
}
