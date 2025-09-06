#
# repo:			cpputils
# name:			flagsandsys_common.pri
# path:			prj/common/common_qt/flagsandsys_common.pri
# created on:   2023 Jun 21
# created by:   Davit Kalantaryan (davit.kalantaryan@desy.de)
# usage:		Use this qt include file to calculate some platform specific stuff
#


message("!!! $${PWD}/flagsandsys_common.pri")

isEmpty(cppDesktopDuplicationSampleFlagsAndSysCommonIncluded){
    cppDesktopDuplicationSampleFlagsAndSysCommonIncluded = 1

    cppDesktopDuplicationSampleRepoRoot = $${PWD}/../../..

    isEmpty(artifactRoot) {
        artifactRoot = $$(artifactRoot)
        isEmpty(artifactRoot) {
            artifactRoot = $${cppDesktopDuplicationSampleRepoRoot}
		}
    }

    include("$${cppDesktopDuplicationSampleRepoRoot}/contrib/cinternal/prj/common/common_qt/flagsandsys_common.pri")

    INCLUDEPATH += $${cppDesktopDuplicationSampleRepoRoot}/include

    exists($${cppDesktopDuplicationSampleRepoRoot}/sys/$${CODENAME}/$$CONFIGURATION/lib) {
        LIBS += -L$${cppDesktopDuplicationSampleRepoRoot}/sys/$${CODENAME}/$$CONFIGURATION/lib
    }
    exists($${cppDesktopDuplicationSampleRepoRoot}/sys/$${CODENAME}/$$CONFIGURATION/tlib) {
        LIBS += -L$${cppDesktopDuplicationSampleRepoRoot}/sys/$${CODENAME}/$$CONFIGURATION/tlib
    }
}
