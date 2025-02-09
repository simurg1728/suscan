#========== Configs ==========#
TEMPLATE = lib

CONFIG += c++11 skip_target_version_ext shared

DEFINES += QT_DEPRECATED_WARNINGS \
           QT_MESSAGELOGCONTEXT \
           no_plugin_name_prefix \
           suscan_BUILD

TARGET = suscan

#========== Version ==========#
VERSION = 0.3.0.0 # major.minor.patch.build
DEFINES += SUSCAN_VERSION_MAJOR=0
DEFINES += SUSCAN_VERSION_MINOR=3
DEFINES += SUSCAN_VERSION_PATCH=0

DEFINES += SUSCAN_ABI_VERSION=1

#========== PRI Files (DO NOT CHANGE ORDER) ==========#
include($$PWD/suscan.pri)

#========== Output directories (DO NOT CHANGE ORDER, IT USES SUSCAN_BUILD_PATH) ==========#
DESTDIR = $$SUSCAN_BUILD_PATH
UI_DIR = $$SUSCAN_BUILD_PATH/.ui
MOC_DIR = $$SUSCAN_BUILD_PATH/.moc
RCC_DIR = $$SUSCAN_BUILD_PATH/.rcc
OBJECTS_DIR = $$SUSCAN_BUILD_PATH/.obj

#========== Add source files ==========#
HEADERS += \
    $$PWD/util/bpe.h \
    $$PWD/util/cbor.h \
    $$PWD/util/com.h \
    $$PWD/util/compat.h \
    $$PWD/util/hashlist.h \
    $$PWD/util/list.h \
    $$PWD/util/macos-barriers.h \
    $$PWD/util/macos-barriers.imp.h \
    $$PWD/util/confdb.h \
    $$PWD/util/cfg.h \
    $$PWD/util/object.h  \
    $$PWD/util/rbtree.h \
    $$PWD/util/sha256.h \
    $$PWD/util/strmap.h \
    $$PWD/util/urlhelpers.h

SOURCES += \
    $$PWD/util/bpe.c \
    $$PWD/util/cbor.c \
    $$PWD/util/cfg.c \
    $$PWD/util/com.c \
    $$PWD/util/compat.c \
    $$PWD/util/confdb.c \
    $$PWD/util/deserialize-xml.c \
    $$PWD/util/deserialize-yaml.c \
    $$PWD/util/hashlist.c \
    $$PWD/util/list.c \
    $$PWD/util/object.c \
    $$PWD/util/rbtree.c \
    $$PWD/util/serialize-xml.c \
    $$PWD/util/serialize-yaml.c \
    $$PWD/util/sha256.c \
    $$PWD/util/strmap.c \
    $$PWD/util/urlhelpers.c

HEADERS += $$files($$PWD/yaml/*.h, true)
SOURCES += $$files($$PWD/yaml/*.c, true)

HEADERS += $$files($$PWD/sgdp4/*.h, true)
SOURCES += $$files($$PWD/sgdp4/*.c, true)

SOURCES += \
    $$PWD/cli/datasavers/csv.c \
    $$PWD/cli/datasavers/mat5.c \
    $$PWD/cli/datasavers/matlab.c \
    $$PWD/cli/datasavers/tcp.c \
    $$PWD/cli/chanloop.c \
    $$PWD/cli/parse.c \
    $$PWD/cli/datasaver.c

HEADERS += $$files($$PWD/analyzer/device/*.h, true)
SOURCES += $$files($$PWD/analyzer/device/*.c, true)
HEADERS += $$files($$PWD/analyzer/inspector/*.h, true)
SOURCES += $$files($$PWD/analyzer/inspector/*.c, true)
HEADERS += $$files($$PWD/analyzer/estimators/*.h, true)
SOURCES += $$files($$PWD/analyzer/estimators/*.c, true)
HEADERS += $$files($$PWD/analyzer/*.h, false)
SOURCES += $$files($$PWD/analyzer/*.c, false)
HEADERS += $$files($$PWD/analyzer/impl/*.h, true)
SOURCES += $$files($$PWD/analyzer/impl/*.c, true)
HEADERS += $$files($$PWD/analyzer/correctors/*.h, true)
SOURCES += $$files($$PWD/analyzer/correctors/*.c, true)
HEADERS += $$files($$PWD/analyzer/workers/*.h, true)
SOURCES += $$files($$PWD/analyzer/workers/*.c, true)
HEADERS += $$files($$PWD/analyzer/source/*.h, true)
SOURCES += $$files($$PWD/analyzer/source/*.c, true)
HEADERS += $$files($$PWD/analyzer/spectsrcs/*.h, true)
SOURCES += $$files($$PWD/analyzer/spectsrcs/*.c, true)
SOURCES += $$PWD/src/lib.c
