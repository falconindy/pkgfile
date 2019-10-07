project('pkgfile', 'cpp',
        version : '21',
        license : 'MIT',
        default_options : [
          'cpp_std=c++17',
          'warning_level=3',
          'prefix=/usr',
        ])

conf = configuration_data()
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('DEFAULT_CACHEPATH', get_option('cachedir'))

# TODO: get rid of this
conf.set('MAX_LINE_SIZE', '(size_t) (1024 * 10)')

cpp = meson.get_compiler('cpp')

configure_file(
    output : 'config.hh',
    configuration : conf)

add_project_arguments('-include', 'config.hh', language : 'cpp')

libpcre = dependency('libpcre', version : '>= 8.30')
libarchive = dependency('libarchive', version : '>= 3.2.0')
libcurl = dependency('libcurl')
pthreads = dependency('threads')
stdcppfs = cpp.find_library('stdc++fs')

pod2man = find_program('pod2man')
pkgconfig = find_program('pkg-config')
mkdir_p = 'mkdir -p $DESTDIR/@0@'

pkgfile_sources = files('''
  src/match.cc src/match.hh
  src/pkgfile.cc src/pkgfile.hh
  src/repo.cc src/repo.hh
  src/result.cc src/result.hh
  src/update.cc src/update.hh
'''.split())

run_target(
  'fmt', command : ['clang-format', '-i', '--style=Google' ] + pkgfile_sources)

executable(
  'pkgfile',
  pkgfile_sources,
  dependencies : [libpcre, libarchive, libcurl, pthreads, stdcppfs],
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

if get_option('systemd_units')
  systemd = dependency(
    'systemd', not_found_message : 'systemd not found but systemd_units=true')
  systemunitdir = systemd.get_pkgconfig_variable('systemdsystemunitdir')

  install_data(
    'systemd/pkgfile-update.timer',
    install_dir : systemunitdir)

  configure_file(
    input : 'systemd/pkgfile-update.service.in',
    output : 'pkgfile-update.service',
    configuration : conf,
    install_dir : systemunitdir)
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
  install_dir : join_paths(get_option('datadir'), 'zsh/site-functions'))

install_data(
  'LICENSE',
  install_dir : join_paths(get_option('datadir'), 'licenses/pkgfile'))

python = import('python')
py3 = python.find_installation('python3')

python_requirement = '>=3.7'
if py3.found() and py3.language_version().version_compare(python_requirement)
  foreach input : [
    'tests/list.py',
    'tests/search.py',
    'tests/update.py',
  ]
    basename = input.split('/')[-1].split('.')[0]

    test(
      'pkgfile_@0@_integration_test'.format(basename),
      py3,
      args : [join_paths(meson.source_root(), input)],
      env : ['PYTHONDONTWRITEBYTECODE=1'])
  endforeach
else
  message('Skipping integration tests, python @0@ not found'.format(
    python_requirement))
endif
