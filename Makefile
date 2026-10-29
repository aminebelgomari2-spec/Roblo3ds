#---------------------------------------------------------------------------------
# Roblo3DS - Makefile (devkitARM / libctru / citro2d)
#---------------------------------------------------------------------------------
.SUFFIXES:

#---------------------------------------------------------------------------------
# Regles officielles devkitARM : creation de .smdh, .3dsx, .elf, bin2o, etc.
#---------------------------------------------------------------------------------
include $(DEVKITARM)/3ds_rules

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=$$DEVKITPRO/devkitARM")
endif

#---------------------------------------------------------------------------------
# Nom cible : coherent avec app.rsf (Title: Roblo3DS), tools/make_cia.sh et build.yml
#---------------------------------------------------------------------------------
TARGET       :=  Roblo3DS
BUILD        :=  build
SOURCES      :=  source
DATA         :=  data
INCLUDES     :=
APP_TITLE    	:=  Roblo3DS
APP_DESCRIPTION  :=  Mini Roblox pour Nintendo 3DS
APP_AUTHOR   :=  Ismahane
ICON         :=  assets/icon.png

#---------------------------------------------------------------------------------
# options de compilation (3DS normale : ARM11 268 MHz, hard float)
#---------------------------------------------------------------------------------
ARCH    :=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  :=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS  +=	$(INCLUDE) -D__3DS__

CXXFLAGS    := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS :=	-g $(ARCH)
LDFLAGS =	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    :=	-lcitro2d -lcitro3d -lctru -lm

LIBDIRS :=	$(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_SOURCES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o)

export OFILES	:=	$(OFILES_BIN) $(OFILES_SOURCES)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# Metadatas pour le fichier .smdh (icone du menu homebrew pour le .3dsx)
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
export APP_ICON	:=	$(CURDIR)/$(ICON)

#---------------------------------------------------------------------------------
.PHONY: $(BUILD) all clean cia

#---------------------------------------------------------------------------------
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).lst $(TARGET).cia $(TARGET).map banner.bnr icon.icn

#---------------------------------------------------------------------------------
# CIA : construit le .cia avec bannertool + makerom via tools/make_cia.sh
#---------------------------------------------------------------------------------
cia: all
	bash tools/make_cia.sh

#---------------------------------------------------------------------------------
%.bin.o %_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(OUTPUT).smdh

$(OFILES_SOURCES)	:	$(HFILES)

$(OUTPUT).elf	:	$(OFILES)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
