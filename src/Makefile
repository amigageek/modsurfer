CC             = m68k-amigaos-gcc
CFLAGS         = -O3 -m68000 -fomit-frame-pointer -mregparm=4
LDFLAGS        = -s
OUTDIR         = build
DEPFLAGS       = -MT $@ -MMD -MP -MF $(OUTDIR)/$*.Td

GENTABLES      = $(OUTDIR)/gentables
GENTABLES_SRCS = gentables.c
TABLES_HDR     = $(OUTDIR)/tables.h

GENIMAGES      = $(OUTDIR)/genimages
GENIMAGES_SRCS = genimages.c
IMAGES_HDR     = $(OUTDIR)/images.h
IMAGES         = images/font.iff images/logo.iff images/pointer.iff images/ball.iff

MODSURFER      = $(OUTDIR)/modsurfer
MODSURFER_SRCS = blit.c common.c dtypes.c game.c gfx.c gfx_opt.asm keyboard.asm main.c menu.c module.c ptplayer/ptplayer.asm system.c track.c
MODSURFER_OBJS = $(patsubst %, $(OUTDIR)/%.o, $(basename $(MODSURFER_SRCS)))

$(shell mkdir -p $(OUTDIR)/ptplayer >/dev/null)

all: $(MODSURFER)

clean:
	rm -fr $(OUTDIR)

dist:
	cp -f $(MODSURFER) ..

$(MODSURFER): $(MODSURFER_OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

$(OUTDIR)/%.o : %.c $(OUTDIR)/%.d
	$(CC) $(DEPFLAGS) $(CFLAGS) -c -o $@ $<
	@mv -f $(OUTDIR)/$*.Td $(OUTDIR)/$*.d && touch $@

$(OUTDIR)/%.o : %.asm
	vasmm68k_mot -DMODSURFER -Faout -o $@ $<

$(OUTDIR)/gfx.o: $(IMAGES_HDR)

$(TABLES_HDR): $(GENTABLES)
	$(GENTABLES) > $@

# FIXME FIXME FIXME FIXME FIXME
$(OUTDIR)/track.o: $(TABLES_HDR)

$(IMAGES_HDR): $(GENIMAGES) $(IMAGES)
	$(GENIMAGES) > $@

$(GENTABLES): $(GENTABLES_SRCS)
	cc -o $@ $^ -lm

$(GENIMAGES): $(GENIMAGES_SRCS)
	cc -o $@ $^

$(OUTDIR)/%.d: ;

.PRECIOUS: $(OUTDIR)/%.d

include $(wildcard $(patsubst %, $(OUTDIR)/%.d, $(basename $(MODSURFER_SRCS))))
