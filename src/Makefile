# -*- tab-width: 8; indent-tabs-mode: t; -*-

AS		= vasmm68k_mot
ASFLAGS		= -DMODSURFER -Faout
CC		= m68k-amigaos-gcc
CFLAGS		= -O3 -fomit-frame-pointer -s -m68000 -mregparm=4
DEPFLAGS	= -MT $@ -MMD -MP -MF $(BUILDDIR)/$*.Td
BUILDDIR		= build

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

MODSURFER	= $(BUILDDIR)/modsurfer
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

$(MODSURFER): $(MODSURFER_OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

$(BUILDDIR)/%.c.o : %.c $(BUILDDIR)/%.d
	$(CC) $(DEPFLAGS) $(CFLAGS) -c -o $@ $<
	@mv -f $(BUILDDIR)/$*.Td $(BUILDDIR)/$*.d && touch $@

$(BUILDDIR)/%.asm.o : %.asm
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILDDIR)/gfx.c.o: $(IMAGES_HDR)
$(BUILDDIR)/track.c.o: $(TABLES_HDR)

$(IMAGES_HDR): $(GENIMAGES) $(IMAGES_SRCS)
	$(GENIMAGES) > $@

$(TABLES_HDR): $(GENTABLES)
	$(GENTABLES) > $@

$(GENTABLES): $(GENTABLES_SRCS)
	cc -o $@ $^ -lm

$(GENIMAGES): $(GENIMAGES_SRCS)
	cc -o $@ $^

$(BUILDDIR)/%.d: ;

.PRECIOUS: $(BUILDDIR)/%.d

include $(wildcard $(patsubst %, $(BUILDDIR)/%.d, $(basename $(MODSURFER_SRCS))))
