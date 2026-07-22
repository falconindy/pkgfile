#!/usr/bin/env python

import hashlib
import os
import pkgfile_test
from pathlib import Path


def _sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class TestUpdate(pkgfile_test.TestCase):
    def setUp(self):
        super().setUp()

        # relocate our cachedir to not overwrite the golden fileset
        self.goldendir = self.cachedir
        self.cachedir = Path(self.tempdir, 'cache')
        self.cachedir.mkdir()

    def assertMatchesGolden(self, reponame):
        golden = Path(self.goldendir, f'{reponame}.files')
        converted = Path(self.cachedir, f'{reponame}.files')

        self.assertEqual(
            _sha256(golden),
            _sha256(converted),
            msg='golden={}, converted={}'.format(golden, converted),
        )

    def testUpdate(self):
        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        self.assertMatchesGolden('multilib')
        self.assertMatchesGolden('testing')

    def testUpdateForcesUpdates(self):
        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        inodes_before = {}
        for repo in ('multilib', 'testing'):
            inodes_before[repo] = Path(self.cachedir, f'{repo}.files').stat().st_ino

        r = self.Pkgfile(['-uu'])
        self.assertEqual(r.returncode, 0)

        inodes_after = {}
        for repo in ('multilib', 'testing'):
            inodes_after[repo] = Path(self.cachedir, f'{repo}.files').stat().st_ino

        for repo in ('multilib', 'testing'):
            self.assertNotEqual(
                inodes_before[repo],
                inodes_after[repo],
                msg='{}.files unexpectedly NOT rewritten'.format(repo),
            )

    def testUpdateSkipsUpToDate(self):
        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        # gather inodes before the update
        inodes_before = {}
        for repo in ('multilib', 'testing'):
            inodes_before[repo] = Path(self.cachedir, f'{repo}.files').stat().st_ino

        # Wind testing.files' own mtime back, so pkgfile believes it's stale
        # and re-downloads it. `pkgfile -u` decides staleness from the db
        # file's on-disk mtime (stamped with the upstream archive's mtime at
        # write time, see DbBuilder::WriteToFile), not any baked-in field.
        testing_path = self.cachedir / 'testing.files'
        os.utime(testing_path, (0, 0))

        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        # re-gather inodes after a soft update
        inodes_after = {}
        for repo in ('multilib', 'testing'):
            inodes_after[repo] = Path(self.cachedir, f'{repo}.files').stat().st_ino

        # compare inodes
        self.assertEqual(
            inodes_before['multilib'],
            inodes_after['multilib'],
            msg='multilib.files unexpectedly rewritten by `pkgfile -u`',
        )

        self.assertNotEqual(
            inodes_before['testing'],
            inodes_after['testing'],
            msg='testing.files unexpectedly NOT rewritten by `pkgfile -u`',
        )

    def testUpdateSkipsBadServer(self):
        Path(self.tempdir / 'pacman.conf').write_text(f"""
            [options]
            Architecture = x86_64

            [testing]
            Server = {self.baseurl}/$arch/$repo/404
            Server = {self.baseurl}/$arch/$repo

            [multilib]
            Server = {self.baseurl}/$arch/$repo
            """)

        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

    def testUpdateFailsWhenExhaustingServers(self):
        Path(self.tempdir, 'pacman.conf').write_text(f"""
            [options]
            Architecture = x86_64

            [testing]
            Server = {self.baseurl}/$arch/$repo/404

            [multilib]
            Server = {self.baseurl}/$arch/$repo
            """)

        r = self.Pkgfile(['-u'])
        self.assertNotEqual(r.returncode, 0)

    def testUpdateRemovesUnknownRepos(self):
        expected_removed = (
            self.cachedir / 'garbage.files',
            self.cachedir / 'deletemebro.files.000',
        )

        for p in expected_removed:
            p.touch()

        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        for p in expected_removed:
            self.assertFalse(p.exists(), msg='{p} still exists, expected deleted')

    def testSkipsCachedirTidyingOnParanoia(self):
        (self.cachedir / 'somedir').mkdir()

        non_repo_files = (
            self.cachedir / 'garbage.files',
            self.cachedir / 'deletemebro.files.000',
        )

        for p in non_repo_files:
            p.touch()

        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        # files which should be removed are still present because we bailed on
        # detecting a directory
        for p in non_repo_files:
            self.assertTrue(p.exists(), msg='{p} still exists, expected deleted')

        self.assertIn('Directory found in pkgfile cachedir', r.stderr.decode())


if __name__ == '__main__':
    pkgfile_test.main()
