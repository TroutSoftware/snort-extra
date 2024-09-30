

ISNORT := /opt/snort/include/snort
SNORT := /opt/snort/bin/snort
OUTPUTDIR := $(abspath p)
MAKEDIR := $(abspath .m)


MODULE_NAME = trout_snort

DEBUG_MODULE := $(MODULE_NAME)_debug.so
RELEASE_MODULE := $(MODULE_NAME).so

.PHONY: format mkrtest premake postmake usage

usage:
	@echo Trout Snort plugins makefile instructions
	@echo 
	@echo To build a release build: make release
	@echo To build a debug build: make build
	@echo To clean all build folders: make clean
	@echo To run clang-format on all source files: make format

mkrtest: 
	@echo -------
	@echo debug: $(DEBUG_MODULE) release: $(RELEASE_MODULE)
	@echo Sub2: $(LIBDEFS)
	@echo deps: $(DEPS)
	@echo objs: $(OBJS)

test: $(OUTPUTDIR)/$(DEBUG_MODULE)
	@echo Testing "$(TEST_DIRS)"
	cd sh3;go install
	sh3 -sanitize none -t $(OUTPUTDIR)/$(DEBUG_MODULE) -tpath "$(TEST_DIRS)" $(TEST_LIMIT)

format:
	$(MAKE) -C ./plugins/dhcp_monitor format
	$(MAKE) -C ./plugins/dhcp_option format
	$(MAKE) -C ./plugins/trout_netflow format

#############################################

define README_CONTENT
  !!!Do NOT store any content you want to keep in this folder!!!

  The folder is automatically generated by the build process and all
  content in it will be deleted at random.
endef

MAKE_README_FILENAME := $(MAKEDIR)/README.TXT

clean:
	if [ -f $(OUTPUTDIR)/$(DEBUG_MODULE) ]; then rm $(OUTPUTDIR)/$(DEBUG_MODULE); fi
	if [ -f $(OUTPUTDIR)/$(RELEASE_MODULE) ]; then rm $(OUTPUTDIR)/$(RELEASE_MODULE); fi
	if [ -d $(MAKEDIR) ]; then rm -r $(MAKEDIR); fi
	@echo "\e[3;32mClean done\e[0m"

release: $(OUTPUTDIR)/$(RELEASE_MODULE) | $(OUTPUTDIR)
	@echo Result output to:  $(OUTPUTDIR)/$(RELEASE_MODULE)
	@echo Release build done!

build: $(OUTPUTDIR)/$(DEBUG_MODULE) | $(OUTPUTDIR)
	@echo Result output to:  $(OUTPUTDIR)/$(DEBUG_MODULE)
	@echo Debug build done!

$(MAKE_README_FILENAME): | $(MAKEDIR)
	$(file >$(MAKE_README_FILENAME),$(README_CONTENT))

$(MAKEDIR):
	mkdir -p $(MAKEDIR)

$(OUTPUTDIR):
	mkdir -p $(OUTPUTDIR)

CC_SOURCES :=
OBJS :=
DEPS :=
TEST_DIRS :=

########################################################################
# Reads FILES from all lib_def.mk files from all subfolders and adds 
# them with correct path to CC_SOURCES
define EXPAND_SOURCEFILES
 $(eval $(file <$(1)))
 SRC_DIR := $(dir $(1))
 ifdef FILES
   CC_SOURCES += $(addprefix $$(SRC_DIR),$(FILES))
 endif
 ifdef TEST_FOLDER
   TEST_DIRS := $(TEST_DIRS)$(addprefix $$(SRC_DIR),$(TEST_FOLDER));
 endif
 undefine FILES
 undefine TEST_FOLDER
 
endef

LIBDEFS = $(shell find $(SOURCEDIR) -name 'lib_def.mk')
$(foreach mk_file,$(LIBDEFS),$(eval $(call EXPAND_SOURCEFILES,$(mk_file))))
########################################################################

OBJS=$(abspath $(addprefix $(MAKEDIR)/, $(subst .cc,.o,$(CC_SOURCES))))
DEPS=$(abspath $(addprefix $(MAKEDIR)/, $(subst .cc,.d,$(CC_SOURCES))))

# Include dependencies if they exists
-include ${DEPS}

# Rule for how to compile .cc files to .o files
$(MAKEDIR)/%.o : %.cc | $(MAKE_README_FILENAME)
	@mkdir -p $(dir $@)
	g++ -MMD -MT '$(patsubst %.cc,$(MAKEDIR)/%.o,$<)' -pipe -O0 -std=c++2b -Wall -fPIC -Wextra -g -I $(ISNORT) -c $< -o $@

# Rule for linking debug build (how to generate $(OUTPUTDIR)/$(DEBUG_MODULE) )
$(OUTPUTDIR)/$(DEBUG_MODULE): $(OBJS) | $(OUTPUTDIR)
	@echo "\e[3;37mLinking...\e[0m"
	g++ $(OBJS) -shared -O0 -Wall -g -Wextra -o $@

# Rule for linking release build (how to generate $(OUTPUTDIR)/$(RELEASE_MODULE) )
$(OUTPUTDIR)/$(RELEASE_MODULE): $(CC_SOURCES) | $(OUTPUTDIR)
	@echo "\e[3;37mLinking...\e[0m"
	g++ -O3 -std=c++2b -fPIC -Wall -Wextra -shared -I $(ISNORT) $(CC_SOURCES) -o $(OUTPUTDIR)/$(RELEASE_MODULE)	

