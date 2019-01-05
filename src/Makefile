# -*- tab-width: 8; indent-tabs-mode: t; -*-

AS		= vasmm68k_mot
ASFLAGS		= -DMODSURFER -Faout
CC		= m68k-amigaos-gcc
CFLAGS		= -O3 -fomit-frame-pointer -s -m68000 -mregparm=4
DEPFLAGS	= -MT $@ -MMD -MP -MF $(OUTDIR)/$*.Td
OUTDIR		= build

GENTABLES	= $(OUTDIR)/gentables
GENTABLES_SRCS	= gentables.c
TABLES_HDR	= $(OUTDIR)/tables.h

GENIMAGES	= $(OUTDIR)/genimages
GENIMAGES_SRCS	= genimages.c
IMAGES_HDR	= $(OUTDIR)/images.h
IMAGES		=		\
	ball.iff		\
	font.iff		\
	logo.iff		\
	pointer.iff
IMAGES_SRCS	= $(addprefix images/, $(IMAGES))

MODSURFER	= $(OUTDIR)/modsurfer
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
MODSURFER_OBJS	= $(patsubst %, $(OUTDIR)/%.o, $(MODSURFER_SRCS))

$(shell mkdir -p $(OUTDIR)/ptplayer >/dev/null)

all: $(MODSURFER)

clean:
	rm -fr $(OUTDIR)

$(MODSURFER): $(MODSURFER_OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

$(OUTDIR)/%.c.o : %.c $(OUTDIR)/%.d
	$(CC) $(DEPFLAGS) $(CFLAGS) -c -o $@ $<
	@mv -f $(OUTDIR)/$*.Td $(OUTDIR)/$*.d && touch $@

$(OUTDIR)/%.asm.o : %.asm
	$(AS) $(ASFLAGS) -o $@ $<

$(OUTDIR)/gfx.c.o: $(IMAGES_HDR)
$(OUTDIR)/track.c.o: $(TABLES_HDR)

$(IMAGES_HDR): $(GENIMAGES) $(IMAGES_SRCS)
	$(GENIMAGES) > $@

$(TABLES_HDR): $(GENTABLES)
	$(GENTABLES) > $@

$(GENTABLES): $(GENTABLES_SRCS)
	cc -o $@ $^ -lm

$(GENIMAGES): $(GENIMAGES_SRCS)
	cc -o $@ $^

$(OUTDIR)/%.d: ;

.PRECIOUS: $(OUTDIR)/%.d

include $(wildcard $(patsubst %, $(OUTDIR)/%.d, $(basename $(MODSURFER_SRCS))))
