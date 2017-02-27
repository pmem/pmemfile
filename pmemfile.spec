Name:		pmemfile
Version:	0.1
Release:	0%{?dist}
Summary:	Persistent memory-backed, userspace implementation of POSIX-like API.
License:	BSD
URL:		http://github.com/marcinslusarz/pmemfile
Source0:	pmemfile-%{version}.tar.gz
#Source0:	https://github.com/marcinslusarz/pmemfile/archive/%{version}.tar.gz#/pmemfile-%{version}.tar.gz

BuildRequires:	glibc-devel
BuildRequires:	cmake
BuildRequires:	pkgconfig
BuildRequires:	libpmemobj-devel
BuildRequires:	libsyscall_intercept-devel

#ExclusiveArch: x86_64

%description
XXX

%package -n libpmemfile-core
Summary: Persistent memory-backed, userspace implementation of POSIX-like API
Group: System Environment/Libraries
%description -n libpmemfile-core
XXX

%files -n libpmemfile-core
%defattr(-,root,root,-)
%{_libdir}/libpmemfile-core.so.*
%license LICENSE
%doc README.md

%package -n libpmemfile-core-devel
Summary: Development files for libpmemfile-core library
Group: Development/Libraries
Requires: libpmemfile-core = %{version}-%{release}
%description -n libpmemfile-core-devel
Development files for libpmemfile-core library

%files -n libpmemfile-core-devel
%defattr(-,root,root,-)
%{_libdir}/libpmemfile-core.so
%{_libdir}/libpmemfile-core.a
%{_libdir}/pkgconfig/libpmemfile-core.pc
%{_includedir}/libpmemfile-core.h
%{_includedir}/libpmemfile-core-stubs.h
%{_mandir}/man3/libpmemfile-core.3.gz
%license LICENSE
%doc

%package -n libpmemfile
Summary: Persistent memory-backed, transaparent (LD_PRELOAD) userspace implementation of POSIX-like API
Group: System Environment/Libraries
%description -n libpmemfile
XXX

%files -n libpmemfile
%defattr(-,root,root,-)
%{_libdir}/libpmemfile.so.*
%{_libdir}/libpmemfile.so
%{_mandir}/man1/libpmemfile.1.gz
%license LICENSE
%doc

%package -n pmemfile-tools
Summary: Tools for pmemfile
Group: System Environment/Libraries
%description -n pmemfile-tools
XXX

%files -n pmemfile-tools
%defattr(-,root,root,-)
%{_bindir}/mkfs.pmemfile
%{_mandir}/man1/mkfs.pmemfile.1.gz
%license LICENSE
%doc

%prep
%setup -q -n %{name}-%{version}

%build
mkdir build && cd build
%cmake .. -DCMAKE_BUILD_TYPE=Release
make %{?_smp_mflags}

%install
cd build
make install DESTDIR=%{buildroot}

%check
cd build
PMEM_IS_PMEM_FORCE=1 ctest -V %{?_smp_mflags}

%post   -n libpmemfile-core -p /sbin/ldconfig
%postun -n libpmemfile-core -p /sbin/ldconfig

%changelog
* Tue Feb 14 2017 Marcin Ślusarz <marcin.slusarz@intel.com> - 0.1-1
- Initial RPM release
