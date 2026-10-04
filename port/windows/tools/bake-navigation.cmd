@echo off
rem Bakes the bots' navigation: halo.exe, with no window, loads every
rem multiplayer map in the maps folder next to it and writes the map's
rem navigation to the navcache folder there. A map with baked navigation
rem starts with bots at once; without it the first game on the map with bots
rem works it out (and keeps it in the save folder).
rem
rem   bake-navigation.cmd        every map
rem   bake-navigation.cmd new    only the maps with no baked navigation yet
rem
rem (port/linux/game/bot_bake.c; port/linux/README.md, Bots)
setlocal
cd /d "%~dp0"
set HALO_BAKE_NAVIGATION=all
if /i "%~1"=="new" set HALO_BAKE_NAVIGATION=new
if exist halo.exe (
	halo.exe
	if errorlevel 1 (
		echo.
		echo Not every map was baked: see the "bake:" lines above.
	) else (
		echo.
		echo Done: the maps' navigation is in the navcache folder, next to maps.
	)
) else (
	echo halo.exe is not in this folder: %~dp0
)
rem (started with a double click: the window stays until a key is pressed)
echo %cmdcmdline% | find /i "%~nx0" >nul && pause
endlocal
