@echo off

cl main.c -c -nologo -Od -Zi -TC -std:clatest -I \include
link main.obj libs\raylib.lib -WX -NOLOGO -DEBUG:FULL -PDB:umb.pdb -OUT:umb.exe
