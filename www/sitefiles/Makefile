SITEBASE = ../htdocs/

HOWTO_FILES = $(HOWTO_SRC) $(HOWTO_TARGET)
HOWTO_BASENAME = HOWTO
HOWTO_SRC = $(HOWTO_BASENAME).sgml
HOWTO_TARGET = $(HOWTO_BASENAME).html\
			$(HOWTO_BASENAME)-1.html \
			$(HOWTO_BASENAME)-2.html \
			$(HOWTO_BASENAME)-3.html \
			$(HOWTO_BASENAME)-4.html \
			$(HOWTO_BASENAME)-5.html \
			$(HOWTO_BASENAME)-6.html \
			$(HOWTO_BASENAME)-7.html

FAQ_FILES = $(FAQ_SRC) $(FAQ_TARGET)
FAQ_BASENAME = FAQ
FAQ_SRC = $(FAQ_BASENAME).sgml
FAQ_TARGET = $(FAQ_BASENAME).html  \
			$(FAQ_BASENAME)-1.html \
			$(FAQ_BASENAME)-2.html \
			$(FAQ_BASENAME)-3.html \
			$(FAQ_BASENAME)-4.html \
			$(FAQ_BASENAME)-5.html \
			$(FAQ_BASENAME)-6.html \
			$(FAQ_BASENAME)-7.html

PAGES = style.css \
		index.html \
		oldnews.html \
		status.html \
		quickstart.html \
		bad-drivers.html \
		compare.html \
		features.html \
		options.html \
		howto.html \
		lists.html \
		links.html \
		logo.jpg \
		tuxsicle.jpg \
		swsusp-logo.jpg \
		LAC.png \
		osdl_logo.png \
		epfl.png \
		cyclades.png \
		linuxfund.gif \
		downloads/index.html \
		downloads/all/HEADER.html \
		downloads/all/README.html \
		downloads/all/.htaccess \
       		sample-oops.png \
		contrib/Swsusp2_and_Bootsplash_on_FC2.doc \
		$(HOWTO_FILES) \
		$(FAQ_FILES) \

DIRS = contrib fedora downloads downloads/all

# changing these rebuilds everything html
PREREQS = Prettify top.html bottom.html

all: directories htmlfiles rawfiles fedora
	@find . $(SITEBASE) -type d -exec chmod 2775 {} \; 2>/dev/null
	@find . $(SITEBASE) -type f -exec chmod 664 {} \; 2>/dev/null
	@chmod 775 Prettify parse_downloads 2>/dev/null || true

htmlfiles: $(patsubst %,$(SITEBASE)/%,$(filter %.html,$(PAGES)))
rawfiles: $(patsubst %,$(SITEBASE)/%,$(filter-out %.html,$(PAGES)))
directories:
	mkdir -p $(patsubst %,$(SITEBASE)/%,$(DIRS))

# fedora stuff just gets copied:
fedora: $(patsubst %,$(SITEBASE)/%,$(wildcard fedora/*))
$(SITEBASE)/fedora/%: fedora/%
	cp $< $@

downloads/index.html: downloads.xml parse_downloads
	./parse_downloads > $@

# HTML files need a bit of processing.
$(SITEBASE)/index.html: index.html $(PREREQS) downloads/index.html
	./Prettify $< > $@

$(SITEBASE)/%.html: %.html $(PREREQS)
	./Prettify $< > $@

# everything else can go through unharmed
$(SITEBASE)/%: %
	cp $< $@

# building the howto:
$(HOWTO_TARGET): $(HOWTO_BASENAME).sgml
	which linuxdoc > /dev/null && linuxdoc -B html $< -H howto-top.txt -F /dev/null || touch $@

# building the faq:
$(FAQ_TARGET): $(FAQ_BASENAME).sgml
	linuxdoc -B html $< -H faq-top.txt -F /dev/null

clean:
	rm -f $(HOWTO_BASENAME)*.html $(FAQ_BASENAME)*.html
	# Not touching the site base directory!

backup: clean
	cd .. && tar czf sitefiles-`date +"%Y%m%d"`.tar.gz sitefiles/

.PHONY: directories htmlfiles rawfiles howto clean backup
