#!/usr/bin/env python

import glob
import hashlib
import pkgfile_test
import os
from collections import defaultdict


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
        self.cachedir = os.path.join(self.tempdir, 'cache')
        os.mkdir(self.cachedir)

    @staticmethod
    def getRepoFiles(subdir, reponame):
        return sorted(glob.glob('{}/{}.files.*'.format(subdir, reponame)))

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
            original_repo = '{}/x86_64/{repo}/{repo}.files'.format(
                self.alpmcachedir, repo=repo)

            for converted_repo in self.getRepoFiles(self.cachedir, repo):
                # Only compare the integer portion of the mtime. we'll only ever
                # get back second precision from a remote server, so any fractional
                # second that's present on our golden repo can be ignored.
                self.assertEqual(int(os.stat(original_repo).st_mtime),
                                 int(os.stat(converted_repo).st_mtime))

    def testUpdateForcesUpdates(self):
        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        inodes_before = {}
        for r in ('multilib', 'testing'):
            for repofile in self.getRepoFiles(self.cachedir, r):
                inodes_before[os.path.basename(repofile)] = os.stat(
                    repofile).st_ino

        r = self.Pkgfile(['-uu'])
        self.assertEqual(r.returncode, 0)

        inodes_after = {}
        for r in ('multilib', 'testing'):
            for repofile in self.getRepoFiles(self.cachedir, r):
                inodes_after[os.path.basename(repofile)] = os.stat(
                    repofile).st_ino

        for r in ('multilib', 'testing'):
            self.assertNotEqual(
                inodes_before,
                inodes_after,
                msg='{}.files unexpectedly NOT rewritten'.format(r))

    def testUpdateSkipsUpToDate(self):
        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        # gather mtimes
        inodes_before = defaultdict(dict)
        for r in ('multilib', 'testing'):
            for repofile in self.getRepoFiles(self.cachedir, r):
                inodes_before[r][repofile] = os.stat(repofile).st_ino

        # set the mtime to the epoch, expect that it gets rewritten on next update
        os.utime(os.path.join(self.cachedir, 'testing.files.000'), (0, 0))

        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        # re-gather mtimes after a soft update
        inodes_after = defaultdict(dict)
        for r in ('multilib', 'testing'):
            for repofile in self.getRepoFiles(self.cachedir, r):
                inodes_after[r][repofile] = os.stat(repofile).st_ino

        # compare to mtimes after
        self.assertEqual(
            inodes_before['multilib'],
            inodes_after['multilib'],
            msg='multilib.files unexpectedly rewritten by `pkgfile -u`')

        self.assertNotEqual(
            inodes_before['testing'],
            inodes_after['testing'],
            msg='testing.files unexpectedly NOT rewritten by `pkgfile -u`')

    def testUpdateSkipsBadServer(self):
        with open(os.path.join(self.tempdir, 'pacman.conf'), 'w') as f:
            f.write('''
            [options]
            Architecture = x86_64

            [testing]
            Server = {fakehttp_server}/$arch/$repo/404
            Server = {fakehttp_server}/$arch/$repo

            [multilib]
            Server = {fakehttp_server}/$arch/$repo
            '''.format(fakehttp_server=self.baseurl))

        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

    def testUpdateFailsWhenExhaustingServers(self):
        with open(os.path.join(self.tempdir, 'pacman.conf'), 'w') as f:
            f.write('''
            [options]
            Architecture = x86_64

            [testing]
            Server = {fakehttp_server}/$arch/$repo/404

            [multilib]
            Server = {fakehttp_server}/$arch/$repo
            '''.format(fakehttp_server=self.baseurl))

        r = self.Pkgfile(['-u'])
        self.assertNotEqual(r.returncode, 0)

    def testUpdateCleansUpOldRepoChunks(self):
        r = self.Pkgfile(['-uu', '--repochunkbytes=5000'])
        self.assertEqual(r.returncode, 0)
        small_chunks = set(glob.glob('{}/testing.files*'.format(
            self.cachedir)))

        # update again, creating ~half as many repo files
        r = self.Pkgfile(['-uu', '--repochunkbytes=200000'])
        self.assertEqual(r.returncode, 0)
        large_chunks = set(glob.glob('{}/testing.files*'.format(
            self.cachedir)))

        # the 100k chunked fileset is a strict subset of the original 5k chunked fileset.
        self.assertLess(large_chunks, small_chunks)


if __name__ == '__main__':
    pkgfile_test.main()
