#!/bin/sh
#
# A simple script to build the manpage for stalonetray using xsltproc.
#
# The logic in this script would've been placed in the meson.build file, but as
# of the time of writing Meson doesn't provide a way to write intermediate
# files from the stdout of a command, instead relying on the command creating
# the file itself.
#
# Plenty of the logic was viable within Meson, but was moved here to simplify
# the meson.build file.
#
# Usage: ./buildman.sh template output stalonetray-version
#
# Requires: sed, xsltproc
#
# TODO: Accept features as arguments to include/exclude parts of the manpage.

template="$1"
output="$2"
version_str="$3"
stylesheet=""

intermediate_xml="$(basename "$template" .in)"

for cmd in sed xsltproc; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "$cmd is required to build the manpage." >&2
    exit 1
  fi
done

for root in \
    /usr/share/sgml/docbook/stylesheet/xsl/nwalsh \
    /usr/share/xml/docbook/stylesheet/docbook-xs \
    /usr/share/xml/docbook/stylesheet/docbook-xsl-nons \
    /usr/share/xml/docbook/xsl-stylesheets \
    /usr/share/sgml/docbook/xsl-stylesheets \
    /usr/share/xml/docbook/xsl-stylesheets-*-nons \
    /usr/share/sgml/docbook/xsl-stylesheets \
    /usr/share/xsl/docbook \
    /usr/local/share/xsl/docbook \
  ; do
  if [ -f "$root/manpages/docbook.xsl" ]; then
    stylesheet="$root/manpages/docbook.xsl"
    break
  fi
done

if [ -z "$stylesheet" ]; then
  echo "Could not find docbook manpage stylesheet." >&2
  exit 2
fi

sed "s/@VERSION_STR@/$version_str/g" "$template" > "$intermediate_xml"
xsltproc -o "$output" "$stylesheet" "$intermediate_xml"

rm -f "$intermediate_xml"

# vim: tabstop=2 shiftwidth=2 expandtab
