#========== Qt Libraries and include directories ==========#
INCLUDEPATH += \
    $$PWD/. \
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
include($$PWD/../libthirdparty/fftwf.pri)
include($$PWD/../libthirdparty/libsndfile-1.2.2.pri)
include($$PWD/../libthirdparty/soapysdr.pri)
include($$PWD/../libthirdparty/json-c-0.18-20240915.pri)
include($$PWD/../libthirdparty/libxml2.pri)
include($$PWD/../libthirdparty/zlib.pri)

# regex.h for C does not exists in MinGW
DEFINES += USE_REGEX_PCRE2

# clock_gettime(...) and clock_getres(...) functions are defined in "pthread_time.h"
LIBS += -lwinpthread
}
