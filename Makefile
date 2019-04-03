CC = gcc
LIB_LINKER_NAME = librtpdepacketizer.so
LIB_VERSION_MAJOR = 1
LIB_VERSION_MINOR = 0
LIB_VERSION_REL = 0
LIB_SO_NAME = $(LIB_LINKER_NAME).$(LIB_VERSION_MAJOR)
LIB_BIN_NAME = $(LIB_LINKER_NAME).$(LIB_VERSION_MAJOR).$(LIB_VERSION_MINOR).$(LIB_VERSION_REL)

PKGS = glib-2.0
CFLAGS += -Wall -O3 -s -fPIC `pkg-config --cflags $(PKGOPTS) $(PKGS)`
LDFLAGS = -shared -rdynamic -Wl,-soname,$(LIB_SO_NAME) -Wl,--gc-sections `pkg-config --libs $(PKGOPTS) $(PKGS)`

OBJS = \
	rtp_depacketizer.o \
	format.o \
	frame.o \
	h264.o \
	opus.o \
	packet.o

all: $(OBJS)
	$(CC) $(LDFLAGS) -o $(LIB_BIN_NAME) $(CFLAGS) $(OBJS)
	ln -sf $(LIB_BIN_NAME) $(LIB_SO_NAME)
	ln -sf $(LIB_BIN_NAME) $(LIB_LINKER_NAME)

%.o: %.c %.h
	$(CC) -c -o $@ $(CFLAGS) $<

clean:
	rm -f *.o
	rm -f $(LIB_BIN_NAME) $(LIB_SO_NAME) $(LIB_LINKER_NAME)

