# $Id$

CONFIG_DIR = /etc/suspend
CONFIG_FILE = $(CONFIG_DIR)/suspend.conf
SCRIPTLET_DIR = $(CONFIG_DIR)/scriptlets.d
SCRIPT_DEST = /usr/local/sbin/suspend.sh

all:
	@echo "There is nothing to build. Try make install"

install:
	@[ -d $(CONFIG_DIR) -o -f $(SCRIPT_DEST) ] && echo -ne "Configuration directory $(CONFIG_DIR) and/or script $(SCRIPT_DEST) already exists.\nAre you sure you want to overwrite it? (y/N) " && read && case $$REPLY in y*|Y*) exit 0 ;; *) echo "Aborting!" ; exit 1 ;; esac || true
	@echo Installing suspend script to $(SCRIPT_DEST)
	@mkdir -p `dirname $(SCRIPT_DEST)`
	@cp -a suspend.sh /usr/local/sbin
	@echo "Installing configuration files to $(CONFIG_DIR)"
	@mkdir -p $(CONFIG_DIR)
	@cp -a suspend.conf $(CONFIG_FILE)
	@mkdir -p $(SCRIPTLET_DIR)
	@for i in scriptlets.d/* ; do cp -a $$i $(SCRIPTLET_DIR) ; done
	@echo "Setting permissions on installed files"
	@chmod 700 $(SCRIPT_DEST) $(SCRIPTLET_DIR) $(SCRIPTLET_DIR)/grub
	@chmod 600 $(CONFIG_FILE)
	@echo "Installed.
	@echo "Edit $(CONFIG_FILE) to taste, and see `basename $(SCRIPT_DEST)` -h for help"

.PHONY: install backup all
