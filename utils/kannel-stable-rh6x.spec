Summary: Kannel SMS/WAP gateway
Name: kannel
Version: 1.0.3
Release: 1
Copyright: Open source, FreeBSD-style license; see COPYING
Group: Applications/Communications
Source: gateway-%{version}.tar.gz
BuildRoot: /var/tmp/%{name}-buildroot
Packager: Peter Gronholm. <peter.gronholm@wapit.com>
Requires: libxml2 >= 2.3.0

%description
Kannel is an Open Source SMS/WAP gateway. WAP is short for Wireless Application
Protocol. It lets the phone act as a simple hypertext browser, but optimizes the 
markup language, scripting language, and the transmission protocols for wireless
use. The optimized protocols are translated to normal Internet protocols by a WAP
gateway. Kannel also works as a SMS gateway for GSM networks. Almost all GSM
phones can send and receive SMS messages, so this is a way to serve many more
clients than just those using WAP phones. 

%prep
rm -rf $RPM_BUILD_ROOT

%setup -n gateway-%{version}
%build
./configure --with-malloc-native --enable-docs
make

%install
make bindir=$RPM_BUILD_ROOT/usr/local/bin suffix=  install
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/init.d
mkdir -p $RPM_BUILD_ROOT/etc/kannel
mkdir -p $RPM_BUILD_ROOT/usr/man/man8

install -m 644 gw/wapkannel.conf $RPM_BUILD_ROOT/etc/kannel/kannel.conf
install -m 644 gw/smskannel.conf $RPM_BUILD_ROOT/etc/kannel
install -m 644 gw/kannel.8 $RPM_BUILD_ROOT/usr/man/man8
install -m 644 test/fakesmsc $RPM_BUILD_ROOT/usr/local/bin
install -m 644 test/fakewap $RPM_BUILD_ROOT/usr/local/bin
install -m 755 utils/kannel-init.d $RPM_BUILD_ROOT/etc/rc.d/init.d/kannel
install -m 644 utils/run_kannel_box.8 $RPM_BUILD_ROOT/usr/man/man8
install -m 644 utils/start-stop-daemon.8 $RPM_BUILD_ROOT/usr/man/man8

strip $RPM_BUILD_ROOT/usr/local/bin/bearerbox
strip $RPM_BUILD_ROOT/usr/local/bin/wapbox
strip $RPM_BUILD_ROOT/usr/local/bin/smsbox
strip $RPM_BUILD_ROOT/usr/local/bin/fakesmsc
strip $RPM_BUILD_ROOT/usr/local/bin/fakewap
strip $RPM_BUILD_ROOT/usr/local/bin/run_kannel_box
strip $RPM_BUILD_ROOT/usr/local/bin/start-stop-daemon
strip $RPM_BUILD_ROOT/usr/local/bin/wmlsc
strip $RPM_BUILD_ROOT/usr/local/bin/wmlsdasm
strip $RPM_BUILD_ROOT/usr/local/bin/seewbmp

%files
%defattr(-,root,root)
%doc README README.docbook README.src README.wmlscript INSTALL AUTHORS COPYING VERSION NEWS ChangeLog doc/ gw/control.html contrib/

/usr/local/bin/
/etc/rc.d/init.d/kannel
/usr/man/man8

%config(noreplace) /etc/kannel/kannel.conf
%config(noreplace) /etc/kannel/smskannel.conf

%post
ln -sf /usr/local/bin/bearerbox /usr/bin/bearerbox
ln -sf /usr/local/bin/wapbox /usr/bin/wapbox
ln -sf /usr/local/bin/smsbox /usr/bin/smsbox
ln -sf /usr/local/bin/start-stop-daemon /usr/bin/start-stop-daemon
ln -sf /usr/local/bin/run_kannel_box /usr/bin/run_kannel_box

%postun
rm -f /usr/bin/bearerbox
rm -f /usr/bin/wapbox
rm -f /usr/bin/smsbox
rm -f /usr/bin/start-stop-daemon
rm -f /usr/bin/run_kannel_box

%clean
rm -rf $RPM_BUILD_ROOT

%changelog
* Fri Mar 30 2001 Peter Gronholm <peter@wapit.com>
- added that Kannel requires libxml2 >= 2.3.0

* Fri Mar 30 2001 Peter Gronholm <peter@wapit.com>
- removed that Kannel requires libxml2 < 2.3.0

* Tue Feb 27 2001 Peter Gronholm <peter@wapit.com>
- moved rm -rf $RPM_BUILD_ROOT from %install to %prep
- added that Kannel requires libxml2 < 2.3.0

* Tue Feb 06 2001 Peter Gronholm <peter@wapit.com>
- install configuration files with %config macro.
- clean $RPM_BUILD_ROOT before and after building the rpm package.
- %postun macro: remove links to Kannel binaries from /usr/bin when uninstalling rpm package.
- removed -b option from install command in  %install macro.

* Tue Jan 02 2001 Peter Gronholm <peter@wapit.com>
- initial release of Kannel rpm package.
