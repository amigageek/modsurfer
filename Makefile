# -*- tab-width: 8; indent-tabs-mode: t; -*-

AS		= vasmm68k_mot
ASFLAGS		= -DMODSURFER -Faout
CC		= m68k-amigaos-gcc
CFLAGS		= -O3 -fomit-frame-pointer -s -m68000 -mregparm=4
DEPFLAGS	= -MT $@ -MMD -MP -MF $(BUILDDIR)/$*.Td
XDF		= xdftool $(DISTDIR)/ModSurfer.adf
BUILDDIR	= build
DISTDIR		= dist

GENTABLES	= $(BUILDDIR)/gentables
GENTABLES_SRCS	= gentables.c
TABLES_HDR	= $(BUILDDIR)/tables.h

GENIMAGES	= $(BUILDDIR)/genimages
GENIMAGES_SRCS	= genimages.c
IMAGES_HDR	= $(BUILDDIR)/images.h
IMAGES		=		\
	font.iff		\
	logo.iff		\
	pointer.iff
IMAGES_SRCS	= $(addprefix images/, $(IMAGES))

GENBALL		= $(BUILDDIR)/genball
GENBALL_SRCS	= genball.c
BALL_HDR	= $(BUILDDIR)/ball.h

MODSURFER	= $(BUILDDIR)/ModSurfer
MODSURFER_SRCS	=		\
	blit.c			\
	common.c		\
	dtypes.c		\
	game.c			\
	gfx.c			\
	gfx.asm			\
	main.c			\
	menu.c			\
	module.c		\
	ptplayer/ptplayer.asm	\
	system.c		\
	system.asm		\
	track.c
MODSURFER_OBJS	= $(patsubst %, $(BUILDDIR)/%.o, $(MODSURFER_SRCS))

$(shell mkdir -p $(BUILDDIR)/ptplayer >/dev/null)

all: $(MODSURFER)

clean:
	rm -fr $(BUILDDIR)
	rm -fr $(BUILDDIR) $(DISTDIR)

dist: $(MODSURFER)
	rm -fr $(DISTDIR)
	mkdir -p $(DISTDIR)/ModSurfer/Source
	cp $(MODSURFER) extra/ModSurfer.info extra/ModSurfer.adf.info extra/ReadMe* $(DISTDIR)/ModSurfer
	cp -r *.c *.h *.asm LICENSE ReadMe extra images ptplayer $(DISTDIR)/ModSurfer/Source/
	cp extra/Drawer.info $(DISTDIR)/ModSurfer.info
	cp extra/Source.info $(DISTDIR)/ModSurfer/Source.info
	cp extra/SourceReadMe.info $(DISTDIR)/ModSurfer/Source/ReadMe.info

	$(XDF) format ModSurfer + boot write extra/bootblock
	$(XDF) write extra/c + write extra/s
	$(XDF) write $(DISTDIR)/ModSurfer/ModSurfer + write $(DISTDIR)/ModSurfer/ModSurfer.info
	$(XDF) write $(DISTDIR)/ModSurfer/ReadMe + write $(DISTDIR)/ModSurfer/ReadMe.info
	$(XDF) write $(DISTDIR)/ModSurfer/Source + write $(DISTDIR)/ModSurfer/Source.info

	cp $(DISTDIR)/ModSurfer.adf $(DISTDIR)/ModSurfer
	cd $(DISTDIR) ; lha a ModSurfer.lha ModSurfer ModSurfer.info

$(MODSURFER): $(MODSURFER_OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

$(BUILDDIR)/%.c.o : %.c $(BUILDDIR)/%.d
	$(CC) $(DEPFLAGS) $(CFLAGS) -c -o $@ $<
	@mv -f $(BUILDDIR)/$*.Td $(BUILDDIR)/$*.d && touch $@

$(BUILDDIR)/%.asm.o : %.asm
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILDDIR)/gfx.c.o: $(IMAGES_HDR) $(BALL_HDR)
$(BUILDDIR)/track.c.o: $(TABLES_HDR)

$(IMAGES_HDR): $(GENIMAGES) $(IMAGES_SRCS)
	$(GENIMAGES) > $@

$(TABLES_HDR): $(GENTABLES)
	$(GENTABLES) > $@

$(GENTABLES): $(GENTABLES_SRCS)
	cc -o $@ $^ -lm

$(GENIMAGES): $(GENIMAGES_SRCS)
	cc -o $@ $^

$(BALL_HDR): $(GENBALL)
	$(GENBALL) > $@

$(GENBALL): $(GENBALL_SRCS)
	cc -o $@ $^ -lm

$(BUILDDIR)/%.d: ;

.PRECIOUS: $(BUILDDIR)/%.d

include $(wildcard $(patsubst %, $(BUILDDIR)/%.d, $(basename $(MODSURFER_SRCS))))

.PHONY: $(DISTDIR)
