SITEBASE = ../htdocs/

HOWTO_FILES = $(HOWTO_SRC) $(HOWTO_TARGET)
HOWTO_BASENAME = Software-suspend
HOWTO_SRC = $(HOWTO_BASENAME).sgml
HOWTO_TARGET = $(HOWTO_BASENAME).html\
			$(HOWTO_BASENAME)-1.html \
			$(HOWTO_BASENAME)-2.html \
			$(HOWTO_BASENAME)-3.html \
			$(HOWTO_BASENAME)-4.html \
			$(HOWTO_BASENAME)-5.html

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
		download.html \
		options.html \
		howto.html \
		lists.html \
		links.html \
		logo.jpg \
		tuxsicle.jpg \
		swsusp-logo.jpg \
		LAC.png \
		osdl_logo.png \
		linuxfund.gif \
		contrib/Swsusp2_and_Bootsplash_on_FC2.doc \
		$(HOWTO_FILES) \
		$(FAQ_FILES) \

DIRS = contrib

# changing these rebuilds everything html
PREREQS = Prettify top.html bottom.html

all: directories htmlfiles rawfiles
	find $(SITEBASE) -type d -exec chmod 2775 {} \;
	find $(SITEBASE) -type f -exec chmod 664 {} \;


htmlfiles: $(patsubst %,$(SITEBASE)/%,$(filter %.html,$(PAGES)))
rawfiles: $(patsubst %,$(SITEBASE)/%,$(filter-out %.html,$(PAGES)))
directories:
	mkdir -p $(patsubst %,$(SITEBASE)/%,$(DIRS))

# HTML files need a bit of processing.
$(SITEBASE)/%.html: %.html $(PREREQS)
	./Prettify $< > $@

# everything else can go through unharmed
$(SITEBASE)/%: %
	cp $< $@

# building the howto:
$(HOWTO_TARGET): $(HOWTO_BASENAME).sgml
	linuxdoc -B html $< -H howto-top.txt -F /dev/null

# building the faq:
$(FAQ_TARGET): $(FAQ_BASENAME).sgml
	linuxdoc -B html $< -H faq-top.txt -F /dev/null

clean:
	rm -f $(HOWTO_BASENAME)*.html $(FAQ_BASENAME)*.html
	# Not touching the site base directory!

backup: clean
	cd .. && tar czf sitefiles-`date +"%Y%m%d"`.tar.gz sitefiles/

push: 
	#cd .. && tar cz sitefiles/ | ssh bernardb@shell.sf.net 'cd swsusp && tar xzv && make -C sitefiles'
	cd .. && rsync -azve ssh --exclude=.svn sitefiles bernardb@shell.sf.net:swsusp/ && ssh sf make -C swsusp/sitefiles

demo: 
	cd .. && rsync -azve ssh --exclude=.svn sitefiles mussel-w:public-html/swsusp/ && ssh mussel-w make -C public-html/swsusp/sitefiles

berlios: 
	cd .. && rsync -azve ssh --exclude=.svn sitefiles berlios:softwaresuspend/ && ssh berlios make -C softwaresuspend/sitefiles

.PHONY: directories htmlfiles rawfiles howto clean backup push demo
