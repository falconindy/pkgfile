#!/usr/bin/env python

import pkgfile_test
from pathlib import Path


class TestDatabase(pkgfile_test.TestCase):

    def setUp(self):
        super().setUp()

        # relocate our cachedir to not overwrite the golden fileset
        self.goldendir = self.cachedir
        self.cachedir = Path(self.tempdir, 'cache')
        self.cachedir.mkdir()

    def testWritesDatabaseVersionMarker(self):
        r = self.Pkgfile(['-u'])

        # writes a .db_version tag with an integer
        self.assertTrue(
            Path(self.cachedir, '.db_version').read_text().isnumeric())

    def testRefusesWrongDatabaseVersion(self):
        r = self.Pkgfile(['-u'])
        self.assertEqual(r.returncode, 0)

        Path(self.cachedir, '.db_version').write_text('42000')

        r = self.Pkgfile(['-l', '--regex', '.*'])
        self.assertNotEqual(0, r.returncode)
        self.assertIn('Database has incorrect version', r.stderr.decode())

    def testRefusesCachedirWithoutVersion(self):
        r = self.Pkgfile(['-l', '--regex', '.*'])
        self.assertNotEqual(0, r.returncode)
        self.assertIn('Database version file not found', r.stderr.decode())


if __name__ == '__main__':
    pkgfile_test.main()
