#!/bin/sh
#
# update-license -- update prefix blocks with LICENSE text
#
# Run this script in gateway root directory to update the 
# current LICENSE text file to all source code files.
#

old="2001-2008 Kannel Group"
new="2001-2009 Kannel Group"

prog=`basename $0`
files=`find -type f ! -name "${prog}" | xargs fgrep -lr "${old}"`
for i in $files; do
  echo ${i}
  cat ${i} | sed "s/${old}/${new}/" > ${i}.new
  mv ${i}.new ${i}
done
