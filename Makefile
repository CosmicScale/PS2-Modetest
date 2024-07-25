EE_BIN = modetestRaw.elf
EE_BIN_STRIPPED = modetestStripped.elf
EE_BIN_PACKED = modetest.elf
EE_OBJS = modetest.o
EE_LIBS = -L$(PS2SDK)/ports/lib -L$(PS2DEV)/gsKit/lib/ -ldmakit -lgskit -ldebug -lc
EE_INCS += -I$(PS2DEV)/gsKit/include
EE_CFLAGS = -I$(PS2DEV)/gsKit/include
EE_LDFLAGS = -L$(PS2DEV)/gsKit/lib -lpad

all: $(EE_BIN_PACKED)

$(EE_BIN_STRIPPED): $(EE_BIN)
	echo "Stripping..."
	$(EE_STRIP) -o $@ $<

$(EE_BIN_PACKED): $(EE_BIN_STRIPPED)
	echo "Compressing..."
	ps2-packer $< $@

clean:
	rm -f $(EE_BIN) $(EE_BIN_STRIPPED) $(EE_BIN_PACKED) $(EE_OBJS)

run: $(EE_BIN_PACKED)
	ps2client execee host:$(EE_BIN_PACKED)

reset:
	ps2client reset

.PHONY: all clean run reset

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal