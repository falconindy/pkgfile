#!/usr/bin/env python

import fakehttp.server
import glob
import multiprocessing
import os.path
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


def FindMesonBuildDir():
    # When run through meson or ninja, we're already in the build dir
    if os.path.exists('.ninja_log'):
        return os.path.curdir

    # When run manually, we're probably in the repo root.
    paths = glob.glob('*/.ninja_log')
    if len(paths) > 1:
        raise ValueError(
            'Multiple build directories found. Unable to proceed.')
    if len(paths) == 0:
        raise ValueError(
            'No build directory found. Have you run "meson build" yet?')

    return os.path.dirname(paths[0])


class TimeLoggingTestResult(unittest.runner.TextTestResult):

    def startTest(self, test):
        self._started_at = time.time()
        super().startTest(test)

    def addSuccess(self, test):
        elapsed = time.time() - self._started_at
        self.stream.write('\n{} ({:.03}s)'.format(self.getDescription(test),
                                                  elapsed))


class TestCase(unittest.TestCase):

    maxDiff = 10000

    def setUp(self):
        self.build_dir = FindMesonBuildDir()
        self._tempdir = tempfile.TemporaryDirectory()
        self.tempdir = Path(self._tempdir.name)
        self.cachedir = os.path.join(
            os.path.dirname(os.path.realpath(__file__)), 'golden/pkgfile')
        self.alpmcachedir = os.path.join(
            os.path.dirname(os.path.realpath(__file__)), 'golden/alpm')

        q = multiprocessing.Queue()
        self.server = multiprocessing.Process(target=fakehttp.server.Serve,
                                              args=(q, ))
        self.server.start()
        self.baseurl = q.get()

        self._WritePacmanConf()

    def tearDown(self):
        self.server.terminate()
        self.server.join()
        self.assertEqual(0, self.server.exitcode,
                         'Server did not exit cleanly')

    def _WritePacmanConf(self):
        with open(os.path.join(self.tempdir, 'pacman.conf'), 'w') as f:
            f.write('''
            [options]
            Architecture = x86_64 x86_64_v3 imnotlistening

            [testing]
            Server = {fakehttp_server}/$arch/$repo

            [multilib]
            Server = {fakehttp_server}/$arch/$repo
            '''.format(fakehttp_server=self.baseurl))

    def Pkgfile(self, args):
        env = {
            'LC_TIME': 'C.UTF-8',
            'LC_COLLATE': 'C.UTF-8',
            'TZ': 'UTC',
            'PATH': '/bin:/usr/bin',
        }

        cmdline = [
            os.path.join(self.build_dir, 'pkgfile'),
            '--config={}/pacman.conf'.format(self.tempdir),
            '--cachedir={}'.format(self.cachedir),
        ] + args

        return subprocess.run(cmdline, env=env, capture_output=True)


def main():
    test_runner = unittest.TextTestRunner(resultclass=TimeLoggingTestResult)
    unittest.main(testRunner=test_runner)
