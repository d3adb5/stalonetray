SUBDIRS=src

if have_xsltproc
man_MANS=stalonetray.1
endif

doc_DIST=stalonetrayrc.sample
if have_xsltproc
doc_DIST+=stalonetray.1 stalonetray.xml stalonetray.html
dist_hook: stalonetray.1 stalonetray.html
endif
all-local: $(doc_DIST)
clean-local:
	rm -f $(doc_DIST)

EXTRA_DIST=$(doc_DIST) stalonetray.spec stalonetray.xml.in stalonetrayrc.sample.in # $(debian_DIST)
MAINTAINERCLEANFILES=stalonetray.1 stalonetray.html stalonetray.spec stalonetray.xml stalonetrayrc.sample

stalonetray.xml: stalonetray.xml.in configure.ac
stalonetrayrc.sample: stalonetrayrc.sample.in configure.ac
stalonetray.spec: stalonetray.spec.in configure.ac

stalonetray.xml \
stalonetrayrc.sample \
stalonetray.spec:
	VERSION_STR=`echo @VERSION@ | sed 's/\([^\.]*\.[^\.]*\)\..*/\1/'`; \
	cat $< | sed "s/@VERSION_STR@/$$VERSION_STR/g" > $@

if have_xsltproc
stalonetray.1: stalonetray.xml
	$(XSLTPROC) $(XSLTPROC_FLAGS) -o $@ $(DOCBOOK_ROOT)/manpages/docbook.xsl stalonetray.xml

stalonetray.html: stalonetray.xml
	$(XSLTPROC) $(XSLTPROC_FLAGS) -o $@ $(DOCBOOK_ROOT)/xhtml/docbook.xsl stalonetray.xml 2>/dev/null
endif
