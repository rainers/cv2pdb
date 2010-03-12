
PROJECT = cvtest

SRC = cvtest.d

DBGDIR = ..\bin\Debug
RELDIR = ..\bin\Release

DMD = c:\l\dmd-2.040\windows\bin\dmd.exe
# DMD = c:\l\dmd-1.045\windows\bin\dmd.exe

CV2PDB_DBG = $(DBGDIR)\cv2pdb.exe
CV2PDB_REL = $(RELDIR)\cv2pdb.exe

DFLAGS = -L/DELEXECUTABLE
LIBS = phobos.lib

default: dbg_exe
release: rel_exe

dbg_exe: $(DBGDIR)\$(PROJECT).exe
rel_exe: $(RELDIR)\$(PROJECT).exe

######################
$(DBGDIR)\$(PROJECT).exe : $(DBGDIR)\$(PROJECT)_cv.exe $(CV2PDB_DBG)
	$(CV2PDB_DBG) $(DBGDIR)\$(PROJECT)_cv.exe $@

$(DBGDIR)\$(PROJECT)_cv.exe : $(SRC) Makefile
	$(DMD) -of$@ -g -unittest $(DFLAGS) @<<
		$(SRC) $(LIBS)
<<NOKEEP


$(RELDIR)\$(PROJECT).exe : $(RELDIR)\$(PROJECT)_cv.exe $(CV2PDB_REL)
	$(CV2PDB_REL) $(RELDIR)\$(PROJECT)_cv.exe $@

$(RELDIR)\$(PROJECT)_cv.exe : $(SRC) Makefile
	$(DMD) -of$@ -g -release -unittest $(DFLAGS) @<<
		$(SRC) $(LIBS)
<<NOKEEP
