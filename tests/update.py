#!/usr/bin/env python

import glob
import hashlib
import pkgfile_test
import os
from collections import defaultdict
from pathlib import Path


def _sha256(path):
    m = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(4096), b''):
            m.update(block)

    return m.hexdigest()


class TestUpdate(pkgfile_test.TestCase):

    def setUp(self):
        super().setUp()

        # relocate our cachedir to not overwrite the golden fileset
        self.goldendir = self.cachedir
        self.cachedir = Path(self.tempdir, 'cache')
        self.cachedir.mkdir()

    @staticmethod
    def getRepoFiles(subdir, reponame):
        return sorted(Path(subdir).glob(f'{reponame}.files.*'))

    def assertMatchesGolden(self, reponame):
        golden_files = self.getRepoFiles(self.goldendir, reponame)
        converted_files = self.getRepoFiles(self.cachedir, reponame)

        for golden, converted in zip(golden_files, converted_files):
            self.assertEqual(_sha256(golden),
                             _sha256(converted),
                             msg="golden={}, converted={}".format(
                                 golden, converted))

    def testUpdate(self):
        r = self.Pkgfile(['-u', '--repochunkbytes=100000'])
        self.assertEqual(r.returncode, 0)

        self.assertMatchesGolden('multilib')
        self.assertMatchesGolden('testing')

        for repo in ('multilib', 'testing'):
            original_repo = Path(self.alpmcachedir, 'x86_64', repo, f'{repo}.files')

            for converted_repo in self.getRepoFiles(self.cachedir, repo):
                # Only compare the integer portion of the mtime. we'll only ever
                # get back second precision from a remote server, so any fractional
                # second that's present on our golden repo can be ignored.
                self.assertEqual(int(original_repo.stat().st_mtime),
                                 int(converted_repo.stat().st_mtime))

    def testUpdateForcesUpdates(self):
        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        inodes_before = {}
        for r in ('multilib', 'testing'):
            for repofile in self.getRepoFiles(self.cachedir, r):
                inodes_before[repofile.name] = repofile.stat().st_ino

        r = self.Pkgfile(['-uu'])
        self.assertEqual(r.returncode, 0)

        inodes_after = {}
        for r in ('multilib', 'testing'):
            for repofile in self.getRepoFiles(self.cachedir, r):
                inodes_after[repofile.name] = repofile.stat().st_ino

        for r in ('multilib', 'testing'):
            self.assertNotEqual(
                inodes_before,
                inodes_after,
                msg='{}.files unexpectedly NOT rewritten'.format(r))

    def testUpdateSkipsUpToDate(self):
        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        # gather inodes before the update
        inodes_before = defaultdict(dict)
        for r in ('multilib', 'testing'):
            for repofile in self.getRepoFiles(self.cachedir, r):
                inodes_before[r][repofile] = repofile.stat().st_ino

        # set the mtime to the epoch, expect that it gets rewritten on next update
        os.utime(self.cachedir / 'testing.files.000', (0, 0))

        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        # re-gather inodes after a soft update
        inodes_after = defaultdict(dict)
        for r in ('multilib', 'testing'):
            for repofile in self.getRepoFiles(self.cachedir, r):
                inodes_after[r][repofile] = repofile.stat().st_ino

        # compare inodes
        self.assertEqual(
            inodes_before['multilib'],
            inodes_after['multilib'],
            msg='multilib.files unexpectedly rewritten by `pkgfile -u`')

        self.assertNotEqual(
            inodes_before['testing'],
            inodes_after['testing'],
            msg='testing.files unexpectedly NOT rewritten by `pkgfile -u`')

    def testUpdateSkipsBadServer(self):
        Path(self.tempdir / 'pacman.conf').write_text(f'''
            [options]
            Architecture = x86_64

            [testing]
            Server = {self.baseurl}/$arch/$repo/404
            Server = {self.baseurl}/$arch/$repo

            [multilib]
            Server = {self.baseurl}/$arch/$repo
            ''')

        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

    def testUpdateFailsWhenExhaustingServers(self):
        Path(self.tempdir, 'pacman.conf').write_text(f'''
            [options]
            Architecture = x86_64

            [testing]
            Server = {self.baseurl}/$arch/$repo/404

            [multilib]
            Server = {self.baseurl}/$arch/$repo
            ''')

        r = self.Pkgfile(['-u'])
        self.assertNotEqual(r.returncode, 0)

    def testUpdateCleansUpOldRepoChunks(self):
        r = self.Pkgfile(['-uu', '--repochunkbytes=5000'])
        self.assertEqual(r.returncode, 0)
        small_chunks = set(Path(self.cachedir).glob('testing.files*'))

        # update again, creating ~half as many repo files
        r = self.Pkgfile(['-uu', '--repochunkbytes=200000'])
        self.assertEqual(r.returncode, 0)
        large_chunks = set(Path(self.cachedir).glob('testing.files*'))

        # the 100k chunked fileset is a strict subset of the original 5k chunked fileset.
        self.assertLess(large_chunks, small_chunks)

    def testUpdateRemovesUnknownRepos(self):
        expected_removed = (
            self.cachedir / "garbage.files",
            self.cachedir / "deletemebro.files.000",
        )

        for p in expected_removed:
            p.touch()

        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        for p in expected_removed:
            self.assertFalse(p.exists(), msg='{p} still exists, expected deleted')


if __name__ == '__main__':
    pkgfile_test.main()
