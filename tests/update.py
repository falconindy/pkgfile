#!/usr/bin/env python

import hashlib
import pkgfile_test
import os


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


    def assertMatchesGolden(self, reponame):
        golden_repo = '{}/{}.files'.format(self.goldendir, reponame)
        converted_repo = '{}/{}.files'.format(self.cachedir, reponame)

        self.assertEqual(_sha256(golden_repo), _sha256(converted_repo))


    def testUpdate(self):
        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        self.assertMatchesGolden('multilib')
        self.assertMatchesGolden('testing')

        for repo in ('multilib', 'testing'):
            original_repo = '{}/x86_64/{repo}/{repo}.files'.format(self.alpmcachedir, repo=repo)
            converted_repo = '{}/{}.files'.format(self.cachedir, repo)

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
            inodes_before[r] = os.stat(
                    os.path.join(self.cachedir, '{}.files'.format(r))).st_ino

        r = self.Pkgfile(['-uu'])
        self.assertEqual(r.returncode, 0)

        inodes_after = {}
        for r in ('multilib', 'testing'):
            inodes_after[r] = os.stat(
                    os.path.join(self.cachedir, '{}.files'.format(r))).st_ino

        for r in ('multilib', 'testing'):
            self.assertNotEqual(inodes_before[r], inodes_after[r],
                    msg='{}.files unexpectedly NOT rewritten'.format(r))


    def testUpdateSkipsUpToDate(self):
        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        inodes = {}
        for r in ('multilib', 'testing'):
            inodes[r] = os.stat(
                    os.path.join(self.cachedir, '{}.files'.format(r))).st_ino

        # set the mtime to the epoch, expect that it gets rewritten on next update
        os.utime(os.path.join(self.cachedir, 'testing.files'), (0, 0))

        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        self.assertEqual(
                inodes['multilib'],
                os.stat(os.path.join(self.cachedir, 'multilib.files')).st_ino,
                msg='multilib.files unexpectedly rewritten by `pkgfile -u`')

        self.assertNotEqual(
                inodes['testing'],
                os.stat(os.path.join(self.cachedir, 'testing.files')).st_ino,
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


if __name__ == '__main__':
    pkgfile_test.main()
