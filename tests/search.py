#!/usr/bin/env python

import pkgfile_test
import textwrap


class TestUpdate(pkgfile_test.TestCase):

    def testSearchBasename(self):
        r = self.Pkgfile(['-s', 'javafx-src.zip'])
        self.assertEqual(r.returncode, 0)

        self.assertEqual(r.stdout.decode(), textwrap.dedent('''
            testing/java-openjfx-src
            testing/java11-openjfx-src
        ''').lstrip('\n'))


    def testSearchVerbose(self):
        r = self.Pkgfile(['-s', '-v', 'javafx-src.zip'])
        self.assertEqual(r.returncode, 0)

        self.assertEqual(r.stdout.decode(), textwrap.dedent('''
            testing/java-openjfx-src 12.0.2.u1-2  	/usr/lib/jvm/java-12-openjfx/javafx-src.zip
            testing/java11-openjfx-src 11.0.3.u1-2	/usr/lib/jvm/java-11-openjfx/javafx-src.zip
        ''').lstrip('\n'))


    def testSearchGlob(self):
        r = self.Pkgfile(['-s', '-g', '/usr/lib/dhcpcd/dhcpcd-hooks/*'])
        self.assertEqual(r.returncode, 0)

        self.assertEqual(r.stdout.decode(), textwrap.dedent('''
            testing/dhcpcd
        ''').lstrip('\n'))


    def testSearchGlobVerboe(self):
        r = self.Pkgfile(['-s', '-v', '-g', '/usr/lib/dhcpcd/dhcpcd-hooks/*'])
        self.assertEqual(r.returncode, 0)

        self.assertEqual(r.stdout.decode(), textwrap.dedent('''
            testing/dhcpcd 8.0.6-1	/usr/lib/dhcpcd/dhcpcd-hooks/01-test
            testing/dhcpcd 8.0.6-1	/usr/lib/dhcpcd/dhcpcd-hooks/02-dump
            testing/dhcpcd 8.0.6-1	/usr/lib/dhcpcd/dhcpcd-hooks/20-resolv.conf
            testing/dhcpcd 8.0.6-1	/usr/lib/dhcpcd/dhcpcd-hooks/30-hostname
        ''').lstrip('\n'))


    def testNotFound(self):
        r = self.Pkgfile(['-s', 'filedoesntexist'])
        self.assertNotEqual(r.returncode, 0)


if __name__ == '__main__':
    pkgfile_test.main()
