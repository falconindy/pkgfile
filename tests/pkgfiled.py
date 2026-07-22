#!/usr/bin/env python

import os
import pkgfile_test
import shutil
import subprocess
import time
from pathlib import Path


class TestPkgfiled(pkgfile_test.TestCase):
    def setUp(self):
        super().setUp()

        self.watchdir = Path(self.tempdir, 'watch')
        self.watchdir.mkdir()
        self.pkgfilecache = Path(self.tempdir, 'cache')
        self.pkgfilecache.mkdir()
        self.confpath = Path(self.tempdir, 'pacman.conf')

        self._daemons = []

    def tearDown(self):
        for proc in self._daemons:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
        super().tearDown()

    # --- helpers ----------------------------------------------------------

    def WriteConfig(self, repos):
        """Write a pacman.conf enabling exactly the named repos."""
        lines = ['[options]', 'Architecture = x86_64', '']
        for repo in repos:
            lines += [f'[{repo}]', f'Server = {self.baseurl}/$arch/$repo', '']
        self.confpath.write_text('\n'.join(lines))

    def ReplaceConfig(self, repos):
        """Like WriteConfig, but atomically replaces the file via rename."""
        tmp = Path(self.tempdir, 'pacman.conf.new')
        lines = ['[options]', 'Architecture = x86_64', '']
        for repo in repos:
            lines += [f'[{repo}]', f'Server = {self.baseurl}/$arch/$repo', '']
        tmp.write_text('\n'.join(lines))
        os.replace(tmp, self.confpath)

    def StageRepo(self, golden_repo, dest_name):
        """Copy a golden .files DB into the watch dir under dest_name."""
        src = Path(self.alpmcachedir, 'x86_64', golden_repo, f'{golden_repo}.files')
        shutil.copy(src, Path(self.watchdir, dest_name))

    def MoveRepoIn(self, golden_repo, dest_name):
        """Stage a repo outside the watch dir, then rename it in (IN_MOVED_TO)."""
        src = Path(self.alpmcachedir, 'x86_64', golden_repo, f'{golden_repo}.files')
        staged = Path(self.tempdir, dest_name)
        shutil.copy(src, staged)
        os.replace(staged, Path(self.watchdir, dest_name))

    def CachedRepos(self):
        """Set of repo names currently present in the pkgfile cache."""
        return {p.name.split('.files')[0] for p in self.pkgfilecache.glob('*.files*')}

    def VersionMarkerPath(self):
        return Path(self.pkgfilecache, '.db_version')

    def ReadVersionMarker(self):
        return int(self.VersionMarkerPath().read_text().strip())

    def RunOneshot(self, extra_args=None):
        return subprocess.run(
            [
                os.path.join(self.build_dir, 'pkgfiled'),
                '--oneshot',
                f'--config={self.confpath}',
            ]
            + (extra_args or [])
            + [str(self.watchdir), str(self.pkgfilecache)],
            capture_output=True,
        )

    def StartDaemon(self):
        logfile = open(Path(self.tempdir, 'pkgfiled.log'), 'wb')
        proc = subprocess.Popen(
            [
                os.path.join(self.build_dir, 'pkgfiled'),
                f'--config={self.confpath}',
                str(self.watchdir),
                str(self.pkgfilecache),
            ],
            stdout=subprocess.DEVNULL,
            stderr=logfile,
        )
        self._daemons.append(proc)
        # The inotify watches are registered in the constructor before the event
        # loop runs, so the kernel queues events from here on; give the process a
        # moment to get there.
        time.sleep(0.3)
        return proc

    def WaitUntil(self, predicate, timeout=10.0, interval=0.02):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return True
            time.sleep(interval)
        return predicate()

    # --- oneshot ----------------------------------------------------------

    def testOneshotSyncsEnabledRepos(self):
        self.WriteConfig(['testing', 'multilib'])
        self.StageRepo('testing', 'testing.files')
        self.StageRepo('multilib', 'multilib.files')

        r = self.RunOneshot()
        self.assertEqual(r.returncode, 0, msg=r.stderr.decode())
        self.assertEqual(self.CachedRepos(), {'testing', 'multilib'})

    def testOneshotSkipsDisabledRepos(self):
        # 'extra' has a .files in the watch dir but is not enabled in the config.
        self.WriteConfig(['testing'])
        self.StageRepo('testing', 'testing.files')
        self.StageRepo('multilib', 'extra.files')

        r = self.RunOneshot()
        self.assertEqual(r.returncode, 0, msg=r.stderr.decode())
        self.assertEqual(self.CachedRepos(), {'testing'})

    # --- .db_version marker ------------------------------------------------

    def testOneshotWritesVersionMarkerOnEmptyCache(self):
        self.WriteConfig([])

        r = self.RunOneshot()
        self.assertEqual(r.returncode, 0, msg=r.stderr.decode())
        self.assertTrue(self.VersionMarkerPath().exists())
        # Just needs to parse as an int; the specific value is an
        # implementation detail of the current db format.
        self.ReadVersionMarker()

    def testOneshotForcesFullResyncWhenMarkerIsOlder(self):
        self.WriteConfig(['testing'])
        self.StageRepo('testing', 'testing.files')

        r = self.RunOneshot()
        self.assertEqual(r.returncode, 0, msg=r.stderr.decode())
        current_version = self.ReadVersionMarker()

        # Simulate a cache left behind by an older pkgfiled: corrupt the
        # repacked repo file, but stamp its mtime ahead of the source .files
        # in the watch dir, so a plain mtime comparison would consider it
        # already up to date and skip it.
        cached_repo = Path(self.pkgfilecache, 'testing.files')
        cached_repo.write_bytes(b'not a real pfdb')
        future = time.time() + 3600
        os.utime(cached_repo, (future, future))
        self.VersionMarkerPath().write_text(str(current_version - 1))

        r = self.RunOneshot()
        self.assertEqual(r.returncode, 0, msg=r.stderr.decode())

        self.assertNotEqual(
            cached_repo.read_bytes(),
            b'not a real pfdb',
            msg='stale-versioned repo was not force-repacked despite an up to '
            'date mtime',
        )
        self.assertEqual(
            self.ReadVersionMarker(),
            current_version,
            msg='version marker was not brought up to date after a full resync',
        )

    def testOneshotFailsOnNewerVersionMarker(self):
        self.WriteConfig(['testing'])
        self.StageRepo('testing', 'testing.files')

        r = self.RunOneshot()
        self.assertEqual(r.returncode, 0, msg=r.stderr.decode())
        current_version = self.ReadVersionMarker()

        newer_version = current_version + 1
        self.VersionMarkerPath().write_text(str(newer_version))

        r = self.RunOneshot()

        self.assertNotEqual(
            r.returncode,
            0,
            msg='pkgfiled did not fail on a database version newer than it understands',
        )
        self.assertEqual(
            self.ReadVersionMarker(),
            newer_version,
            msg='version marker newer than this pkgfiled understands was overwritten',
        )

    # --- live inotify on the sync dir ------------------------------------

    def testWatchedRepoIsRepackedOnMove(self):
        self.WriteConfig(['testing'])
        proc = self.StartDaemon()

        self.MoveRepoIn('testing', 'testing.files')

        self.assertTrue(
            self.WaitUntil(lambda: 'testing' in self.CachedRepos()),
            msg='enabled repo was not repacked after appearing in the watch dir',
        )
        self.assertIsNone(proc.poll(), msg='daemon exited unexpectedly')

    def testUnwatchedRepoIsIgnoredOnMove(self):
        self.WriteConfig(['testing'])
        self.StartDaemon()

        # 'multilib' is not enabled; move it in first, then an enabled repo. The
        # kernel delivers inotify events in order, so once 'testing' shows up we
        # know 'multilib' has already been seen (and skipped).
        self.MoveRepoIn('multilib', 'multilib.files')
        self.MoveRepoIn('testing', 'testing.files')

        self.assertTrue(self.WaitUntil(lambda: 'testing' in self.CachedRepos()))
        self.assertNotIn(
            'multilib',
            self.CachedRepos(),
            msg='disabled repo was repacked despite not being enabled',
        )

    # --- config changes ---------------------------------------------------

    def testConfigChangeRemovesDisabledRepo(self):
        self.WriteConfig(['testing', 'multilib'])
        self.StageRepo('testing', 'testing.files')
        self.StageRepo('multilib', 'multilib.files')
        self.StartDaemon()

        # initial sync repacks both
        self.assertTrue(
            self.WaitUntil(lambda: self.CachedRepos() == {'testing', 'multilib'})
        )

        # disable multilib via an in-place config edit
        self.WriteConfig(['testing'])

        self.assertTrue(
            self.WaitUntil(lambda: self.CachedRepos() == {'testing'}),
            msg='cache for disabled repo was not removed on config change',
        )

    def testConfigChangeSyncsNewRepo(self):
        # both .files are present on disk, but only testing is enabled initially
        self.WriteConfig(['testing'])
        self.StageRepo('testing', 'testing.files')
        self.StageRepo('multilib', 'multilib.files')
        self.StartDaemon()

        self.assertTrue(self.WaitUntil(lambda: self.CachedRepos() == {'testing'}))

        # enable multilib via an atomic rename-replace of the config
        self.ReplaceConfig(['testing', 'multilib'])

        self.assertTrue(
            self.WaitUntil(lambda: self.CachedRepos() == {'testing', 'multilib'}),
            msg='newly enabled repo was not synced after config change',
        )


if __name__ == '__main__':
    pkgfile_test.main()
