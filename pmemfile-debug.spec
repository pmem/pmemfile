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

%package -n libpmemfile-core-debug
Summary: Persistent memory-backed, userspace implementation of POSIX-like API
Group: System Environment/Libraries
%description -n libpmemfile-core-debug
XXX

%files -n libpmemfile-core-debug
%defattr(-,root,root,-)
%{_libdir}/pmemfile_debug/libpmemfile-core.so.*
%{_libdir}/pmemfile_debug/libpmemfile-core.so
%{_libdir}/pmemfile_debug/libpmemfile-core.a
%license LICENSE

%package -n libpmemfile-debug
Summary: Persistent memory-backed, transaparent (LD_PRELOAD) userspace implementation of POSIX-like API
Group: System Environment/Libraries
%description -n libpmemfile-debug
XXX

%files -n libpmemfile-debug
%defattr(-,root,root,-)
%{_libdir}/pmemfile_debug/libpmemfile.so.*
%{_libdir}/pmemfile_debug/libpmemfile.so
%license LICENSE
%doc

%prep
%setup -q -n %{name}-%{version}

%build
%cmake . -DCMAKE_BUILD_TYPE=Debug
make %{?_smp_mflags}

%install
make install DESTDIR=%{buildroot}

%check
ctest -V %{?_smp_mflags}

%changelog
* Tue Feb 14 2017 Marcin Ślusarz <marcin.slusarz@intel.com> - 0.1-1
- Initial RPM release
