manpages: hibernate.conf.5 hibernate.8

hibernate.conf.5 hibernate.8:
	bash gen-manpages.sh

install: manpages install.sh
	bash install.sh

clean:
	rm -f hibernate.conf.5 hibernate.8

.PHONY: manpages clean
