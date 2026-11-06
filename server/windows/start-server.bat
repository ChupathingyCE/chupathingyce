@echo off
rem Chupathingy Dedicated Server. Set your playlist and your server's name
rem below, save, and double-click this file. README.txt has the rest.
rem
rem   start-server.bat                  runs the server in this window
rem   start-server.bat install [name]   makes a Windows service of it, with
rem                                     these settings (as administrator)
rem   start-server.bat uninstall [name] takes the service out again

rem the data folder: this file's (maps\, playlists\, logs\, saves\)
cd /d "%~dp0"

rem ---------- your settings

rem the playlist, in the playlists folder
set HALO_DEDICATED=playlists\free_for_all.txt
rem the server's name (15 characters at most)
set HALO_DEDICATED_NAME=My Server

rem players a game takes, and how many it waits for
rem set HALO_DEDICATED_MAXIMUM_PLAYERS=16
rem set HALO_DEDICATED_MINIMUM_PLAYERS=1
rem false: not listed in the in-game Server Browser
rem set HALO_DEDICATED_PUBLIC=true
rem the control panel: on this machine only by default
rem (http://127.0.0.1:8080); another port, an address, or off
rem set HALO_DEDICATED_CONTROL=off
rem a fixed UDP port for internet play, for a firewall rule
rem set HALO_NET_TUNNEL_PORT=2302
rem the server's saves: the saves folder here, as a service's, not your
rem own game's (%APPDATA%\ChupathingyCE)
set "HALO_SAVE_ROOT=%~dp0saves"

rem ---------- the end of the settings

if /i "%~1"=="install" goto install
if /i "%~1"=="uninstall" goto uninstall

chupathingyce-server.exe
echo.
echo The server stopped.
pause
goto :eof

:install
chupathingyce-server.exe --install-service %2
pause
goto :eof

:uninstall
chupathingyce-server.exe --uninstall-service %2
pause
goto :eof
