CONFIG_DIR = /etc/suspend.d
CONFIG_FILE = $(CONFIG_DIR)/suspend.conf
SCRIPTLET_DIR = $(CONFIG_DIR)/scriptlets

all:
	@echo "There is nothing to build. Try make install"

backup:
	@echo "Backing up old configuration files, if any."
	@[ -f $(CONFIG_FILE) ] && /bin/mv $(CONFIG_FILE) $(CONFIG_FILE).bak || true
	@[ -d $(SCRIPTLET_DIR) ] && /bin/mv $(SCRIPTLET_DIR) $(SCRIPTLET_DIR).bak || true

install: backup
	@echo "Installing suspend script to /usr/local/sbin/suspend.sh"
	@mkdir -p /usr/local/sbin
	@cp -a suspend.sh /usr/local/sbin
	@echo "Installing configuration files to /etc/suspend.d"
	@mkdir -p $(CONFIG_DIR)
	@cp -a suspend.conf $(CONFIG_FILE)
	@cp -a scriptlets $(CONFIG_DIR)

.PHONY: install backup all
