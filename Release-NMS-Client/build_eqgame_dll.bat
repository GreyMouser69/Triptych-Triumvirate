@echo off
cd /d "%~dp0"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" eqgame_dll.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /p:WindowsTargetPlatformVersion=10.0.19041.0 /p:PlatformToolset=v145 /m /v:m /nologo
