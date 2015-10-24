@echo off
cl /nologo /MT /Ot /DWINDOWS /Feusbagb.exe *.c ..\common\crc32.c
del *.obj
echo on
