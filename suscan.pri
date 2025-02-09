#========== Qt Libraries and include directories ==========#
INCLUDEPATH += \
    $$PWD/analyzer \
    $$PWD/cli \
    $$PWD/src \
    $$PWD/util \
    $$PWD/yaml

#========== Get build configuration and library path (ATTENTION: It DOES NOT use shadow build path for binaries)==========#
include(configuration.pri)
SUSCAN_BUILD_PATH = $$PWD/build/$${PLATFORM}/$${BUILD_CONFIG}/$${CUR_ARCH}
DEPENDPATH += $${SUSCAN_BUILD_PATH}

#========== Libraries ==========#
!contains(DEFINES, suscan_BUILD) {
    LIB_NAME = suscan
    win32|unix: LIBS += -L$${SUSCAN_BUILD_PATH} -l$${LIB_NAME}
}

include($$PWD/../sigutils/sigutils.pri)

win32 {
# Add from Radioconda's packages
LIBS += -L$${RADIOCONDA_PATH}/Library/lib -lsndfile
LIBS += -L$${RADIOCONDA_PATH}/Library/lib -lSoapySDR
LIBS += -L$${RADIOCONDA_PATH}/Library/lib -llibxml2
LIBS += -L$${RADIOCONDA_PATH}/Library/lib -lfftw3
LIBS += -L$${RADIOCONDA_PATH}/Library/lib -lzlib

# Libxml2 is located in a folder
INCLUDEPATH += $${RADIOCONDA_PATH}/Library/include/libxml2

# We use custom build json-c since Radioconda doesn't have it
include($$PWD/../libthirdparty/json-c-0.18-20240915.pri)

# regex.h for C does not exists in MinGW
DEFINES += USE_REGEX_PCRE2

# clock_gettime(...) and clock_getres(...) functions are defined in "pthread_time.h"
LIBS += -lwinpthread
}
