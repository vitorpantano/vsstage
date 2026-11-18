@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
echo ==============================================
echo   VS Stage - compilar para Windows
echo ==============================================

rem --- Caminho 1 (recomendado): MSYS2 / MinGW-w64 (gera um .exe unico, sem DLLs)
set MSYS=C:\msys64\mingw64\bin
if exist "%MSYS%\g++.exe" (
  echo Usando MSYS2 MinGW-w64...
  set "PATH=%MSYS%;%PATH%"
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DVSS_BUILD_TESTS=ON || goto :erro
  cmake --build build -j || goto :erro
  goto :ok
)

rem --- Caminho 2: Visual Studio 2022 (Build Tools)
where cl >nul 2>nul
if %errorlevel%==0 (
  echo Usando Visual Studio...
  cmake -S . -B build -A x64 -DVSS_BUILD_TESTS=ON || goto :erro
  cmake --build build --config Release -j || goto :erro
  if not exist build\Release\VSStage.exe goto :erro
  copy /y build\Release\VSStage.exe build\VSStage.exe >nul
  goto :ok
)

echo.
echo Nao encontrei um compilador. Instale UM dos dois:
echo   * MSYS2 (https://www.msys2.org) e depois, no terminal "MSYS2 MINGW64":
echo       pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja git
echo   * ou Visual Studio Build Tools 2022 (carga "Desenvolvimento para desktop com C++") e rode este
echo     arquivo a partir do "Developer Command Prompt for VS 2022".
echo.
echo Ou use o GitHub Actions (veja o LEIA-ME): ele compila na nuvem, sem instalar nada.
pause
exit /b 1

:ok
echo.
echo Pronto: build\VSStage.exe
echo Testes do nucleo:
set VSS_AUDIO_BACKEND=null
if exist build\mapper_test.exe build\mapper_test.exe
if exist build\core_test.exe build\core_test.exe
pause
exit /b 0

:erro
echo.
echo A compilacao falhou. Copie as mensagens acima e envie para analise.
pause
exit /b 1
