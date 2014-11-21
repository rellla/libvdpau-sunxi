include Make.config

TARGET = libvdpau_sunxi.so.1
SRC = device.c presentation_queue.c surface_output.c surface_video.c \
	surface_bitmap.c video_mixer.c decoder.c handles.c ve.c \
	h264.c mpeg12.c mp4.c rgba.c tiled_yuv.S

GLIBINCLUDES ?= $(shell export PKG_CONFIG_PATH=${TOOLCHAIN}/pkgconfig; pkg-config --cflags glib-2.0)
GLIBLIBS ?= $(shell export PKG_CONFIG_PATH=${TOOLCHAIN}/pkgconfig; pkg-config --libs glib-2.0)

CFLAGS ?= -Wall -O3
LDFLAGS ?=
LIBS ?= -lrt -lm -lX11 $(GLIBLIBS)
CC ?= gcc

INCLUDES += $(GLIBINCLUDES)

MAKEFLAGS += -rR --no-print-directory

DEP_CFLAGS ?= -MD -MP -MQ $@
LIB_CFLAGS ?= -fpic
LIB_LDFLAGS ?= -shared -Wl,-soname,$(TARGET)

OBJ = $(addsuffix .o,$(basename $(SRC)))
DEP = $(addsuffix .d,$(basename $(SRC)))

MODULEDIR = $(shell export PKG_CONFIG_PATH=${TOOLCHAIN}/pkgconfig; pkg-config --variable=moduledir vdpau)

ifeq ($(MODULEDIR),)
MODULEDIR=/usr/lib/vdpau
endif

.PHONY: clean all install

all: $(TARGET)
$(TARGET): $(OBJ)
	$(CC) $(LIB_LDFLAGS) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

clean:
	rm -f $(OBJ)
	rm -f $(DEP)
	rm -f $(TARGET)

install: $(TARGET)
	install -D $(TARGET) $(DESTDIR)$(MODULEDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(MODULEDIR)/$(TARGET)

%.o: %.c
	$(CC) $(INCLUDES) $(DEP_CFLAGS) $(LIB_CFLAGS) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) -c $< -o $@

include $(wildcard $(DEP))
