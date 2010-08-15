#!/bin/sh

VERSION=`head -1 ../VERSION`
export VERSION
DATE=`date +%Y-%m-%d`

echo Making WAkannel-${VERSION}.pkg
cd ..
make 
make install
cd solaris
sed "s/VERSION_NUM/${VERSION}/" < prototype.tmpl > prototype
sed "s/VERSION_NUM/${VERSION}/" < pkginfo.tmpl > pkginfo
TRG=./kannel
mkdir ${TRG}
mkdir ${TRG}/bin
mkdir ${TRG}/sbin
mkdir ${TRG}/man
cp ../wmlscript/wmlsc ${TRG}/bin/wmlsc-${VERSION}
cp ../wmlscript/wmlsdasm ${TRG}/bin/wmlsdasm-${VERSION}
cp ../utils/seewbmp ${TRG}/bin/seewbmp-${VERSION}
cp ../gw/bearerbox ${TRG}/sbin/bearerbox-${VERSION}
cp ../gw/smsbox ${TRG}/sbin/smsbox-${VERSION}
cp ../gw/wapbox ${TRG}/sbin/wapbox-${VERSION}
cp ../utils/run_kannel_box ${TRG}/sbin/run_kannel_box-${VERSION}
cp ../utils/seewbmp.1 ${TRG}/man/man1/seewbmp.1 
cp ../wmlscript/wmlsc.1 ${TRG}/man/man1/wmlsc.1
cp ../wmlscript/wmlsdasm.1 ${TRG}/man/man1/wmlsdasm.1
cp ../gw/kannel.8 ${TRG}/man/man8/kannel.8
cp ../utils/run_kannel_box.8 ${TRG}/man/man8/run_kannel_box.8

pkgmk -r `pwd` -d . -o
pkgtrans . WAkannel-${VERSION}.pkg WAkannel-${VERSION}
