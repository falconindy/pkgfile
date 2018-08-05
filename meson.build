project('pkgfile', 'c',
        version : '17',
        license : 'MIT',
        default_options : [
          'c_std=c99',
          'prefix=/usr',
        ])

conf = configuration_data()
conf.set('_GNU_SOURCE', true)
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('CACHEPATH', get_option('cachedir'))

cc = meson.get_compiler('c')
if cc.has_function('mempcpy', args: '-D_GNU_SOURCE')
  conf.set('HAVE_MEMPCPY', true)
endif

configure_file(
    output : 'config.h',
    configuration : conf)

add_project_arguments('-include', 'config.h', language : 'c')

libpcre = dependency('libpcre')
libarchive = dependency('libarchive')
libcurl = dependency('libcurl')
pthreads = dependency('threads')

pod2man = find_program('pod2man')
pkgconfig = find_program('pkg-config')
mkdir_p = 'mkdir -p $DESTDIR/@0@'

if get_option('systemd_units')
  systemdsystemunitdir = run_command(
    pkgconfig, '--variable=systemdsystemunitdir', 'systemd').stdout().strip()
endif

pkgfile_sources = files('''
  src/match.c src/match.h
  src/pkgfile.c src/pkgfile.h
  src/repo.c src/repo.h
  src/result.c src/result.h
  src/update.c src/update.h
  src/macro.h src/missing.h
'''.split())

run_target(
  'fmt', command : ['clang-format', '-i', '--style=Google' ] + pkgfile_sources)

executable(
  'pkgfile',
  pkgfile_sources,
  dependencies : [libpcre, libarchive, libcurl, pthreads],
  install : true)

man = custom_target(
    'man',
    output : 'pkgfile.1',
    input : 'README.pod',
    command : [
      pod2man,
      '--section=1',
      '--center=pkgfile Manual',
      '--name=PKGFILE',
      '--release=pkgfile @0@'.format(meson.project_version()),
      '@INPUT@', '@OUTPUT@'
    ],
    install : true,
    install_dir : join_paths(get_option('mandir'), 'man1'))

systemd_units = files('''
  systemd/pkgfile-update.service
  systemd/pkgfile-update.timer
'''.split())

if systemdsystemunitdir != ''
  install_data(
    systemd_units,
    install_dir : systemdsystemunitdir)
endif

command_not_found_hooks = files('''
  extra/command-not-found.bash
  extra/command-not-found.zsh
  extra/command-not-found.fish
'''.split())

meson.add_install_script('sh', '-c', mkdir_p.format(get_option('cachedir')))

install_data(
  command_not_found_hooks,
  install_dir : join_paths(get_option('datadir'), 'doc/pkgfile'))

install_data(
  'extra/bash-completion',
  rename : ['pkgfile'],
  install_dir : join_paths(get_option('datadir'), 'bash-completion/completions'))

install_data(
  'extra/zsh-completion',
  rename : ['_pkgfile'],
  install_dir : join_paths(get_option('datadir'), 'zsh/site-functions '))
