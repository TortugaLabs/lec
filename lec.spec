Summary: Linux Ethernet Console
Name: lec
Version: 0.2.0
Release: 1
License: GPL,BSD2
Group: System Environment/Base
URL: http://www.0ink.net
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
Linux Ethernet Console.

%prep
%setup -q

%build
make VERSION=%{version}-%{release} OPTFLAGS="%{optflags}"

%install
rm -rf $RPM_BUILD_ROOT
make \
	DESTDIR=$RPM_BUILD_ROOT \
	SBINDIR=/sbin \
	MANDIR=%{_mandir} \
	install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
/sbin/*

%doc %{_mandir}/man?/*
%doc cec.txt COPYING HACKING.cec README.*

%changelog
* Fri Mar 11 2011 Alejandro Liu Ly <alex@0ink.net> - 0.2.0-1
- Simplify the coding.  

* Sat Aug 22 2009 Alejandro Liu Ly <alex@chengdu.0ink.net> - 0.1.4-1
- Partial fix to controlling tty issues.
- If pid=1, cecd is child process, otherwise, cecd is parent process.
- Improve the way network is brought up.

* Wed Aug 19 2009 Alejandro Liu Ly <alex@chengdu.0ink.net> - 0.1.0-1
- Initial version

