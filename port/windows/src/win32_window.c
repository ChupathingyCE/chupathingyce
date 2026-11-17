/*
WIN32_WINDOW.C

The borderless window on Windows (display.mode = "borderless": SDL's
fullscreen without a display mode, sdl_platform.c). SDL makes it a topmost
popup the size of the display, and some graphics drivers (NVIDIA's OpenGL)
take such a window for exclusive fullscreen: whenever it gains or loses the
focus the display blanks for a moment as for a mode change, for seconds on a
display that uses Display Stream Compression. Without the popup and topmost
styles it is an ordinary window with no frame, the same size, which the
desktop shows as it does any other. SDL sets its styles again on some changes
(the focus, a mode), so they are looked at again before each frame is shown.
*/

#include <windows.h>

/* the borderless window's styles, if SDL's popup ones are back (the window
handle SDL gives its window, as a pointer: sdl_platform.c sees no Windows
headers) */
void win32_window_borderless_styles(void *window_handle)
{
	HWND window = (HWND)window_handle;
	LONG_PTR style;
	LONG_PTR without_popup;

	if (!window)
		return;
	style = GetWindowLongPtrW(window, GWL_STYLE);
	without_popup = style & ~(LONG_PTR)WS_POPUP;
	if (without_popup != style)
		SetWindowLongPtrW(window, GWL_STYLE, without_popup);
	if (without_popup != style || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST))
	{
		SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
}
