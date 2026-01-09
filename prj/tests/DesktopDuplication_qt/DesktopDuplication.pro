#
# file:		focust_analyze.pro
# path:		prj/ui/focust_analyze_qt/focust_analyze.pro
# created on:	2022 Apr 16
# creatd by:	Davit Kalantaryan (davit.kalantaryan@desy.de)
#

message("!!! $${_PRO_FILE_}")

include ( "$${PWD}/../../common/common_qt/flagsandsys_common.pri" )
QMAKE_CXXFLAGS -= $${CinternalStrongWarings}
QMAKE_CFLAGS -= $${CinternalStrongWarings}

LIBS += -lDXGI
LIBS += -lD3D11

# QT += svg
QT += widgets

UI_SRC_DIR = $${cppDesktopDuplicationSampleRepoRoot}/src/examples/from_msdn

SOURCES +=	$$files($$UI_SRC_DIR/*.cpp,false)
HEADERS +=	$$files($$UI_SRC_DIR/*.hpp)
HEADERS +=	$$files($$UI_SRC_DIR/*.h)
