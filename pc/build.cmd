@echo off
cl /nologo /MT /Ot /DWINDOWS /Feusbagb.exe *.c
del *.obj
echo on
