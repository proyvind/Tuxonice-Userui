CONFIG_DIR = /etc/suspend
CONFIG_FILE = $(CONFIG_DIR)/suspend.conf
SCRIPTLET_DIR = $(CONFIG_DIR)/scriptlets.d
SCRIPT_DEST = /usr/local/sbin/suspend.sh

all:
	@echo "There is nothing to build. Try make install"

backup:
	@echo "Backing up old configuration files, if any."
	@[ -f $(CONFIG_FILE) ] && /bin/mv $(CONFIG_FILE) $(CONFIG_FILE).bak || true
	@[ -d $(SCRIPTLET_DIR) ] && /bin/mv $(SCRIPTLET_DIR) $(SCRIPTLET_DIR).bak || true

install: backup
	@echo Installing suspend script to $(SCRIPT_DEST)
	@mkdir -p `dirname $(SCRIPT_DEST)`
	@cp -a suspend.sh /usr/local/sbin
	@echo "Installing configuration files to $(CONFIG_DIR)"
	@mkdir -p $(CONFIG_DIR)
	@cp -a suspend.conf $(CONFIG_FILE)
	@cp -a scriptlets.d $(CONFIG_DIR)
	@echo "Setting permissions on installed files"
	@chmod 700 $(SCRIPT_DEST) $(SCRIPTLET_DIR) $(SCRIPTLET_DIR)/grub
	@chmod 600 $(CONFIG_FILE)

.PHONY: install backup all
