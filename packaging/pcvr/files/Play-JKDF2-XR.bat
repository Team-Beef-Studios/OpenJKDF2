@echo off
setlocal EnableExtensions
title JKDF2-XR

rem The user can start this from a shortcut, so move to the folder that holds the game.
pushd "%~dp0"

set "EXE=jkdf2xr.exe"

if not exist "%EXE%" goto :no_exe

rem The packaging step always creates "mots", so test for real assets, not for the folders.
set "HAVE_DF2="
set "HAVE_MOTS="
if exist "episode\JK1.gob" if exist "resource\Res2.gob" set "HAVE_DF2=1"
if exist "mots\episode\JKM.goo" if exist "mots\resource\Jkmres.goo" set "HAVE_MOTS=1"

if not defined HAVE_DF2 if not defined HAVE_MOTS goto :no_assets
if not defined HAVE_MOTS goto :run_df2
if not defined HAVE_DF2 goto :run_mots

:menu
echo.
echo   ==================================================
echo    JKDF2-XR   -   Select a game
echo   ==================================================
echo.
echo      1   Jedi Knight: Dark Forces II
echo      2   Mysteries of the Sith
echo      Q   Quit
echo.

where choice >nul 2>&1
if errorlevel 1 goto :menu_prompt

choice /c 12Q /n /m "   Press 1, 2 or Q: "
if errorlevel 3 goto :done
if errorlevel 2 goto :run_mots
goto :run_df2

:menu_prompt
set "PICK="
set /p "PICK=   Type 1, 2 or Q, then press Enter: "
if /i "%PICK%"=="1" goto :run_df2
if /i "%PICK%"=="2" goto :run_mots
if /i "%PICK%"=="Q" goto :done
goto :menu

:run_df2
call :check_mots_token
if errorlevel 1 goto :done
echo.
echo   Start Jedi Knight: Dark Forces II ...
start "" "%EXE%" %*
goto :done

:run_mots
echo.
echo   Start Mysteries of the Sith ...
start "" "%EXE%" -motsCompat %*
goto :done

rem The engine adds commandline.txt to every launch. A -motsCompat line there
rem overrides the menu, so Dark Forces II cannot start while the line is present.
:check_mots_token
if not exist "commandline.txt" exit /b 0
findstr /i /c:"-motsCompat" "commandline.txt" >nul 2>&1
if errorlevel 1 exit /b 0
echo.
echo   The file commandline.txt contains -motsCompat.
echo   The engine reads that file on every launch. It starts
echo   Mysteries of the Sith, even if you select Dark Forces II.
echo.
echo   Delete -motsCompat from commandline.txt. Then run this file again.
echo.
pause
exit /b 1

:no_exe
echo.
echo   ERROR: %EXE% is not in this folder.
echo.
echo   Keep Play-JKDF2-XR.bat in the JKDF2-XR folder, next to %EXE%.
echo.
pause
goto :done

:no_assets
echo.
echo   ERROR: No game files found.
echo.
echo   JKDF2-XR does not include game assets. You must supply them.
echo.
echo   For Jedi Knight: Dark Forces II, copy into this folder:
echo       episode\    (JK1.GOB, JK1CTF.GOB, JK1MP.GOB)
echo       resource\   (Res1hi.gob, Res2.gob, jk_.cd, video\)
echo       player\
echo.
echo   For Mysteries of the Sith, copy into the mots folder:
echo       episode\    (JKM.GOO, JKM_KFY.GOO, JKM_MP.GOO, JKM_SABER.GOO)
echo       resource\   (JKMRES.GOO, JKMsndLO.goo, JK_.CD, VIDEO\)
echo.
echo   Read HOW-TO-PLAY.txt for the full instructions.
echo.
pause
goto :done

:done
popd
endlocal
exit /b 0
