Name: hibernate
Version: 1.02
Release: 1
License: GPL
Group: Applications/System
URL: http://dagobah.ucc.asn.au/swsusp/script2/hibernate-script-1.00.tar.gz
Source0: hibernate-script-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-root
Summary: software suspend 2 hibernate script

%description

hibernate is a shell script that handles the process of getting ready
to suspend to disk and to resume from disk. It requires the Software
Suspend 2 patches available at http://softwaresuspend.berlios.de/
After installing you will want to run 'hibernate -h' to see available
options and modify your /etc/hibernate/hibernate.conf to set them. 

%prep
mkdir -p ${RPM_BUILD_ROOT}/usr/share/doc/hibernate-%version-%release

%setup -n hibernate-script-%version

%install
export BASE_DIR=${RPM_BUILD_ROOT}
export PREFIX=/usr
sh install.sh

%clean
unset BASE_DIR
unset PREFIX
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)

# Directories owned by this package
%dir /usr/share/hibernate

# Files owned by this package (taken from %{buildroot}
/usr/sbin/hibernate
/usr/share/hibernate/*
/usr/man/*
%config /etc/hibernate/*

# Documentation for this package (taken from $RPM_BUILD_DIR)
%doc CHANGELOG
%doc COPYING
%doc README
%doc SCRIPTLET-API
%doc TODO

%changelog
* Wed Nov 24 2004 Bernard Blackham <bernard@blackham.com.au> -
- Updated to 1.02 final version
* Thu Nov 18 2004 Bernard Blackham <bernard@blackham.com.au> -
- Updated to 1.01 final version
* Sun Nov  7 2004 Bernard Blackham <bernard@blackham.com.au> -
- Updated to 1.00 final version
* Fri Aug 20 2004 Bernard Blackham <bernard@blackham.com.au> -
- Updated to 0.99 final version
* Fri Aug 20 2004 Bernard Blackham <bernard@blackham.com.au> -
- Updated to 0.98 final version
* Thu Jul 29 2004 Bernard Blackham <bernard@blackham.com.au> -
- Updated to 0.97 final version
* Sat Jul 24 2004 Bernard Blackham <bernard@blackham.com.au> -
- Updated to 0.96 final version
* Sat Jul 24 2004 Bernard Blackham <bernard@blackham.com.au> -
- Updated to 0.96-rc2 version
* Sat Jul 24 2004 Bernard Blackham <bernard@blackham.com.au> -
- Updated to 0.96-rc1 version
* Fri Jul 23 2004 Bernard Blackham <bernard@blackham.com.au> -
- Updated to 0.95.1 final version
* Wed Jul 21 2004 Kevin Fenzi <kevin@tummy.com> -
- Updated to 0.95 final version
* Fri Jul 16 2004 Kevin Fenzi <kevin@tummy.com> -
- Updated to 0.95-rc1 version
* Mon Jul 12 2004 Kevin Fenzi <kevin@tummy.com> - 
- Initial build.
