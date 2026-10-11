@echo off
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
cl /nologo /std:c++17 /EHsc /W4 /WX /I"Source\RallySurvival\Public" "Tests\PartConditionRulesTests.cpp" /Fo"Intermediate\RuleTests\PartConditionRulesTests.obj" /Fe"Intermediate\RuleTests\PartConditionRulesTests.exe"
if errorlevel 1 (
    popd
    exit /b 1
)
"Intermediate\RuleTests\PartConditionRulesTests.exe"
set "RALLY_TEST_EXIT=%ERRORLEVEL%"
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /I"Source\RallySurvival\Public" "Tests\VehiclePhysicsTests.cpp" /Fo"Intermediate\RuleTests\VehiclePhysicsTests.obj" /Fe"Intermediate\RuleTests\VehiclePhysicsTests.exe"
if errorlevel 1 (
    popd
    exit /b 1
)
"Intermediate\RuleTests\VehiclePhysicsTests.exe"
if errorlevel 1 set "RALLY_TEST_EXIT=1"
popd
exit /b %RALLY_TEST_EXIT%
