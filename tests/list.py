#!/usr/bin/env python

import pkgfile_test
import textwrap


class TestUpdate(pkgfile_test.TestCase):

    def testListExact(self):
        r = self.Pkgfile(['-l', 'dhcpcd'])
        self.assertEqual(r.returncode, 0)

        self.assertEqual(r.stdout.decode(), textwrap.dedent('''
            testing/dhcpcd	/etc/
            testing/dhcpcd	/etc/dhcpcd.conf
            testing/dhcpcd	/usr/
            testing/dhcpcd	/usr/bin/
            testing/dhcpcd	/usr/bin/dhcpcd
            testing/dhcpcd	/usr/lib/
            testing/dhcpcd	/usr/lib/dhcpcd/
            testing/dhcpcd	/usr/lib/dhcpcd/dev/
            testing/dhcpcd	/usr/lib/dhcpcd/dev/udev.so
            testing/dhcpcd	/usr/lib/dhcpcd/dhcpcd-hooks/
            testing/dhcpcd	/usr/lib/dhcpcd/dhcpcd-hooks/01-test
            testing/dhcpcd	/usr/lib/dhcpcd/dhcpcd-hooks/02-dump
            testing/dhcpcd	/usr/lib/dhcpcd/dhcpcd-hooks/20-resolv.conf
            testing/dhcpcd	/usr/lib/dhcpcd/dhcpcd-hooks/30-hostname
            testing/dhcpcd	/usr/lib/dhcpcd/dhcpcd-run-hooks
            testing/dhcpcd	/usr/lib/systemd/
            testing/dhcpcd	/usr/lib/systemd/system/
            testing/dhcpcd	/usr/lib/systemd/system/dhcpcd.service
            testing/dhcpcd	/usr/lib/systemd/system/dhcpcd@.service
            testing/dhcpcd	/usr/share/
            testing/dhcpcd	/usr/share/dhcpcd/
            testing/dhcpcd	/usr/share/dhcpcd/hooks/
            testing/dhcpcd	/usr/share/dhcpcd/hooks/10-wpa_supplicant
            testing/dhcpcd	/usr/share/dhcpcd/hooks/15-timezone
            testing/dhcpcd	/usr/share/dhcpcd/hooks/29-lookup-hostname
            testing/dhcpcd	/usr/share/licenses/
            testing/dhcpcd	/usr/share/licenses/dhcpcd/
            testing/dhcpcd	/usr/share/licenses/dhcpcd/LICENSE
            testing/dhcpcd	/usr/share/man/
            testing/dhcpcd	/usr/share/man/man5/
            testing/dhcpcd	/usr/share/man/man5/dhcpcd.conf.5.gz
            testing/dhcpcd	/usr/share/man/man8/
            testing/dhcpcd	/usr/share/man/man8/dhcpcd-run-hooks.8.gz
            testing/dhcpcd	/usr/share/man/man8/dhcpcd.8.gz
            testing/dhcpcd	/var/
            testing/dhcpcd	/var/lib/
            testing/dhcpcd	/var/lib/dhcpcd/
        ''').lstrip('\n'))


    def testListRegex(self):
        r = self.Pkgfile(['-l', '-r', 'java.*src'])
        self.assertEqual(r.returncode, 0)

        self.assertEqual(r.stdout.decode(), textwrap.dedent('''
            testing/java-openjfx-src  	/usr/
            testing/java-openjfx-src  	/usr/lib/
            testing/java-openjfx-src  	/usr/lib/jvm/
            testing/java-openjfx-src  	/usr/lib/jvm/java-12-openjfx/
            testing/java-openjfx-src  	/usr/lib/jvm/java-12-openjfx/javafx-src.zip
            testing/java-openjfx-src  	/usr/share/
            testing/java-openjfx-src  	/usr/share/licenses/
            testing/java-openjfx-src  	/usr/share/licenses/java-openjfx-src
            testing/java11-openjfx-src	/usr/
            testing/java11-openjfx-src	/usr/lib/
            testing/java11-openjfx-src	/usr/lib/jvm/
            testing/java11-openjfx-src	/usr/lib/jvm/java-11-openjfx/
            testing/java11-openjfx-src	/usr/lib/jvm/java-11-openjfx/javafx-src.zip
            testing/java11-openjfx-src	/usr/share/
            testing/java11-openjfx-src	/usr/share/licenses/
            testing/java11-openjfx-src	/usr/share/licenses/java11-openjfx-src
        ''').lstrip('\n'))


    def testListBinaries(self):
        r = self.Pkgfile(['-l', '-b', 'dhcpcd'])
        self.assertEqual(r.returncode, 0)

        self.assertEqual(r.stdout.decode(), textwrap.dedent('''
            testing/dhcpcd	/usr/bin/dhcpcd
        ''').lstrip('\n'))


    def testListQuiet(self):
        r = self.Pkgfile(['-l', '-q', 'java-openjfx-src'])
        self.assertEqual(r.returncode, 0)

        self.assertEqual(r.stdout.decode(), textwrap.dedent('''
            /usr/
            /usr/lib/
            /usr/lib/jvm/
            /usr/lib/jvm/java-12-openjfx/
            /usr/lib/jvm/java-12-openjfx/javafx-src.zip
            /usr/share/
            /usr/share/licenses/
            /usr/share/licenses/java-openjfx-src
        ''').lstrip('\n'))


    def testListWithRepo(self):
        r = self.Pkgfile(['-l', 'testing/java-openjfx-src'])
        self.assertEqual(r.returncode, 0)

        self.assertEqual(r.stdout.decode(), textwrap.dedent('''
            testing/java-openjfx-src	/usr/
            testing/java-openjfx-src	/usr/lib/
            testing/java-openjfx-src	/usr/lib/jvm/
            testing/java-openjfx-src	/usr/lib/jvm/java-12-openjfx/
            testing/java-openjfx-src	/usr/lib/jvm/java-12-openjfx/javafx-src.zip
            testing/java-openjfx-src	/usr/share/
            testing/java-openjfx-src	/usr/share/licenses/
            testing/java-openjfx-src	/usr/share/licenses/java-openjfx-src
        ''').lstrip('\n'))


    def testListRaw(self):
        r = self.Pkgfile(['-l', '-w', '-r', 'java.*-openjfx-src'])
        self.assertEqual(r.returncode, 0)

        self.assertEqual(r.stdout.decode(), textwrap.dedent('''
            testing/java-openjfx-src	/usr/
            testing/java-openjfx-src	/usr/lib/
            testing/java-openjfx-src	/usr/lib/jvm/
            testing/java-openjfx-src	/usr/lib/jvm/java-12-openjfx/
            testing/java-openjfx-src	/usr/lib/jvm/java-12-openjfx/javafx-src.zip
            testing/java-openjfx-src	/usr/share/
            testing/java-openjfx-src	/usr/share/licenses/
            testing/java-openjfx-src	/usr/share/licenses/java-openjfx-src
            testing/java11-openjfx-src	/usr/
            testing/java11-openjfx-src	/usr/lib/
            testing/java11-openjfx-src	/usr/lib/jvm/
            testing/java11-openjfx-src	/usr/lib/jvm/java-11-openjfx/
            testing/java11-openjfx-src	/usr/lib/jvm/java-11-openjfx/javafx-src.zip
            testing/java11-openjfx-src	/usr/share/
            testing/java11-openjfx-src	/usr/share/licenses/
            testing/java11-openjfx-src	/usr/share/licenses/java11-openjfx-src
        ''').lstrip('\n'))


    def testNotFound(self):
        r = self.Pkgfile(['-l', 'packagedoesntexist'])
        self.assertNotEqual(r.returncode, 0)


if __name__ == '__main__':
    pkgfile_test.main()
