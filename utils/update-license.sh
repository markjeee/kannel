#!/bin/sh
#
# update-license -- update prefix blocks with LICENSE text
#
# Run this script in gateway root directory to update the 
# current LICENSE text file to all source code files.
#

types="\.h \.c \.def"

for t in $types; do
  echo "Updating LICENSE in '${t}' files."
  files=`find -type f | grep "${t}\$"`
  for i in $files; do
    cat LICENSE ${i} >> ${i}.new
    mv ${i}.new ${i}
  done
done
