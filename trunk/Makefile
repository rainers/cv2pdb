#
# makefile to be used with NMAKE for building source and
# installation packages
#
# prerequisites:
# - environment variable VSINSTALLDIR must point to your Visual Studio
#     or VCExpress installation folder
# - Info-zip must be found in the PATH
#
# run
#   nmake src
# to create a source package with name cv2pdb_src_<VERSION>.zip in
# ..\downloads
#
# run
#   nmake bin
# to create a binary package with name cv2pdb_<VERSION>.zip in
# ..\downloads

SRC = src\cv2pdb.cpp \
      src\cv2pdb.h \
      src\demangle.cpp \
      src\demangle.h \
      src\dwarf2pdb.cpp \
      src\dwarf.h \
      src\LastError.h \
      src\main.cpp \
      src\mscvpdb.h \
      src\mspdb.h \
      src\mspdb.cpp \
      src\PEImage.cpp \
      src\PEImage.h \
      src\symutil.cpp \
      src\symutil.h \
      src\dviewhelper\dviewhelper.cpp

ADD = Makefile \
      src\cv2pdb.vcproj \
      src\dviewhelper\dviewhelper.vcproj \
      src\cv2pdb.sln

DOC = VERSION README INSTALL LICENSE CHANGES TODO FEATURES autoexp.expand autoexp.visualizer

BIN = bin\Release\cv2pdb.exe bin\Release\dviewhelper.dll

TEST = test\cvtest.d \
      test\cvtest.vcproj \
      test\Makefile \

all: bin src

###############################
include VERSION

DOWNLOADS = ..\downloads

$(DOWNLOADS):
	if not exist $(DOWNLOADS)\nul mkdir $(DOWNLOADS)

###############################
SRC_ZIP = $(DOWNLOADS)\cv2pdb_src_$(VERSION).zip

src: $(DOWNLOADS) $(SRC_ZIP)

$(SRC_ZIP): $(SRC) $(ADD) $(DOC) $(TEST)
	if exist $(SRC_ZIP) del $(SRC_ZIP)
	zip $(SRC_ZIP) -X $(SRC) $(ADD) $(DOC) $(TEST)
 
###############################
BIN_ZIP = $(DOWNLOADS)\cv2pdb_$(VERSION).zip

bin: $(DOWNLOADS) $(BIN_ZIP)

$(BIN_ZIP): $(BIN) $(DOC) Makefile
	if exist $(BIN_ZIP) del $(BIN_ZIP)
	zip $(BIN_ZIP) -j -X $(BIN) $(DOC)

IDEDIR = $(VSINSTALLDIR)\Common7\IDE

$(BIN): $(SRC) $(ADD) $(TEST) VERSION
	if     exist "$(IDEDIR)\VCExpress.exe" "$(IDEDIR)\VCExpress.exe" /Build Release src\cv2pdb.sln
	if not exist "$(IDEDIR)\VCExpress.exe" "$(IDEDIR)\devenv.exe" /Build Release src\cv2pdb.sln
