@echo off
rem Renders the procedural car sounds to Saved\SoundPreview\*.wav and checks them (docs/SOUND.md).
setlocal
set "RALLY_VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%RALLY_VCVARS%" (
    echo Visual Studio 2022 Community C++ tools not found.
    exit /b 1
)
call "%RALLY_VCVARS%" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if not exist "Intermediate\RuleTests" mkdir "Intermediate\RuleTests"
if not exist "Saved\SoundPreview" mkdir "Saved\SoundPreview"
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /I"Source\RallySurvival\Public" "Tests\SoundPreview.cpp" /Fo"Intermediate\RuleTests\SoundPreview.obj" /Fe"Intermediate\RuleTests\SoundPreview.exe"
if errorlevel 1 (
    popd
    exit /b 1
)
"Intermediate\RuleTests\SoundPreview.exe" "Saved\SoundPreview"
set "RALLY_TEST_EXIT=%ERRORLEVEL%"
popd
exit /b %RALLY_TEST_EXIT%
