@echo off

cl main.c -c -nologo -Od -Zi -TC -std:clatest
link main.obj -WX -NOLOGO -DEBUG:FULL -PDB:umb.pdb -OUT:umb.exe
