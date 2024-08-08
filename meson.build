project(
    'pkgfile',
    'cpp',
    version: '22',
    license: 'MIT',
    meson_version: '>=0.56',
    default_options: ['cpp_std=c++20', 'warning_level=3', 'prefix=/usr'],
)

conf = configuration_data()
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('DEFAULT_CACHEPATH', get_option('cachedir'))
conf.set_quoted('DEFAULT_PACMAN_CONF', '/etc/pacman.conf')

cpp = meson.get_compiler('cpp')

if cpp.get_define('ARCHIVE_FILTER_ZSTD', args: ['-include', 'archive.h']) == ''
    conf.set('ARCHIVE_FILTER_ZSTD', 14)
endif

configure_file(output: 'config.hh', configuration: conf)

add_project_arguments(
    '-include',
    'config.hh',
    '-fno-exceptions',
    language: 'cpp',
)

libpcre = dependency('libpcre', version: '>= 8.30')
libarchive = dependency('libarchive', version: '>= 3.2.0')
libcurl = dependency('libcurl')
libsystemd = dependency('libsystemd')
pthreads = dependency('threads')
stdcppfs = cpp.find_library('stdc++fs')
gtest = dependency('gtest', required: false)
gmock = dependency('gmock', required: false)

pod2man = find_program('pod2man')
pkgconfig = find_program('pkg-config')
mkdir_p = 'mkdir -p $DESTDIR/@0@'

libcommon = static_library(
    'pkgfile',
    files(
        '''
    src/archive_converter.cc src/archive_converter.hh
    src/archive_io.cc src/archive_io.hh
    src/archive_reader.cc src/archive_reader.hh
    src/compress.cc src/compress.hh
    src/filter.cc src/filter.hh
    src/repo.cc src/repo.hh
    src/result.cc src/result.hh
    src/update.cc src/update.hh
  '''.split(),
    ),
    dependencies: [libpcre, libarchive, libcurl, pthreads, stdcppfs],
    install: false,
)

run_target(
    'fmt',
    command: [
        join_paths(meson.project_source_root(), 'build-aux/source-format'),
    ],
)

executable(
    'pkgfile',
    files(
        '''
    src/pkgfile.cc src/pkgfile.hh
  '''.split(),
    ),
    link_with: [libcommon],
    install: true,
)

executable(
    'pkgfiled',
    'src/pkgfiled.cc',
    link_with: [libcommon],
    dependencies: [libsystemd, stdcppfs],
    install: true,
)

custom_target(
    'pkgfile.1',
    output: 'pkgfile.1',
    input: 'man/pkgfile.pod',
    command: [
        pod2man,
        '--section=1',
        '--center=pkgfile Manual',
        '--name=PKGFILE',
        '--release=pkgfile @0@'.format(meson.project_version()),
        '@INPUT@',
        '@OUTPUT@',
    ],
    install: true,
    install_dir: join_paths(get_option('mandir'), 'man1'),
)

custom_target(
    'pkgfiled.1',
    output: 'pkgfiled.1',
    input: 'man/pkgfiled.pod',
    command: [
        pod2man,
        '--section=1',
        '--center=pkgfiled Manual',
        '--name=PKGFILED',
        '--release=pkgfiled @0@'.format(meson.project_version()),
        '@INPUT@',
        '@OUTPUT@',
    ],
    install: true,
    install_dir: join_paths(get_option('mandir'), 'man1'),
)

if get_option('systemd_units')
    systemd = dependency(
        'systemd',
        not_found_message: 'systemd not found but systemd_units=true',
    )
    systemunitdir = systemd.get_variable(pkgconfig: 'systemdsystemunitdir')

    install_data('systemd/pkgfile-update.timer', install_dir: systemunitdir)

    configure_file(
        input: 'systemd/pkgfile-update.service.in',
        output: 'pkgfile-update.service',
        configuration: conf,
        install_dir: systemunitdir,
    )

    configure_file(
        input: 'systemd/pkgfiled.service.in',
        output: 'pkgfiled.service',
        configuration: conf,
        install_dir: systemunitdir,
    )
endif

command_not_found_hooks = files(
    '''
  extra/command-not-found.bash
  extra/command-not-found.zsh
  extra/command-not-found.fish
'''.split(),
)

meson.add_install_script('sh', '-c', mkdir_p.format(get_option('cachedir')))

install_data(
    command_not_found_hooks,
    install_dir: join_paths(get_option('datadir'), 'doc/pkgfile'),
)

install_data(
    'extra/bash-completion',
    rename: ['pkgfile'],
    install_dir: join_paths(get_option('datadir'), 'bash-completion/completions'),
)

install_data(
    'extra/zsh-completion',
    rename: ['_pkgfile'],
    install_dir: join_paths(get_option('datadir'), 'zsh/site-functions'),
)

install_data(
    'LICENSE',
    install_dir: join_paths(get_option('datadir'), 'licenses/pkgfile'),
)

if gtest.found() and gmock.found()
    gtest_main = static_library('gtest_main', 'src/test/gtest_main.cc')

    test(
        'filter_test',
        executable(
            'filter_test',
            files(
                '''
        src/filter_test.cc
      '''.split(),
            ),
            link_with: [libcommon, gtest_main],
            dependencies: [gmock, gtest, libpcre],
        ),
    )
else
    message('Skipping unit tests, gtest or gmock not found')
endif

python = import('python')
py3 = python.find_installation('python3')

python_requirement = '>=3.7'
if py3.found() and py3.language_version().version_compare(python_requirement)
    foreach input : ['tests/list.py', 'tests/search.py', 'tests/update.py']
        basename = input.split('/')[-1].split('.')[0]

        test(
            'pkgfile_@0@_integration_test'.format(basename),
            py3,
            args: [join_paths(meson.project_source_root(), input)],
            env: ['PYTHONDONTWRITEBYTECODE=1'],
        )
    endforeach
else
    message(
        'Skipping integration tests, python @0@ not found'.format(
            python_requirement,
        ),
    )
endif
