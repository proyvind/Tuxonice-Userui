Name: hibernate
Version: 0.95.1
Release: 1
License: GPL
Group: Applications/System
URL: http://dagobah.ucc.asn.au/swsusp/script2/hibernate-script-0.95.1.tar.gz
Source0: hibernate-script-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-root
Summary: software suspend 2 hibernate script

%description

hibernate is a shell script that handles the process of getting ready
to suspend to disk and to resume from disk. It requires the Software
Suspend 2 patches available at http://swsusp.sf.net/ 
After installing you will want to run 'hibernate -h' to see available
options and modify your /etc/hibernate/hibernate.conf to set them. 

%prep
mkdir -p ${RPM_BUILD_ROOT}/usr/share/doc/hibernate-%version-%release

%setup -n hibernate-script-%version

%install
export BASE_DIR=${RPM_BUILD_ROOT}
export PREFIX=/usr
sh install.sh
cp README CHANGELOG COPYING TODO SCRIPTLET-API ${RPM_BUILD_ROOT}/usr/share/doc/hibernate-%version-%release

%clean
unset BASE_DIR
unset PREFIX
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
/usr/local/sbin/hibernate
/usr/local/share/hibernate/*
%doc
/usr/share/doc/hibernate-%version-%release/*
/usr/local/man/*
%config
/etc/hibernate/*

%changelog
* Fri Jul 23 2004 Bernard Blackham <bernard@blackham.com.au> -
- Updated to 0.95.1 final version
* Wed Jul 21 2004 Kevin Fenzi <kevin@tummy.com> -
- Updated to 0.95 final version
* Fri Jul 16 2004 Kevin Fenzi <kevin@tummy.com> -
- Updated to 0.95-rc1 version
* Mon Jul 12 2004 Kevin Fenzi <kevin@tummy.com> - 
- Initial build.
