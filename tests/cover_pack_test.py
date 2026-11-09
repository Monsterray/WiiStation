#!/usr/bin/env python3
"""Tests for scripts/cover_pack.py.  Run:  python -I tests/cover_pack_test.py

Fixture: tests/fixtures/cover_pack (a tiny fake dat and fake image files whose bytes name their
origin, so a copy can be traced). Output goes to a temp directory, never into the repo.
"""
import contextlib
import io
import os
import pathlib
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "scripts"))
import cover_pack as cp  # noqa: E402

FIX = HERE / "fixtures" / "cover_pack"


def run(*extra, out):
    """Run main() with the fixture sources; returns (exit code, stdout, stderr)."""
    argv = ["--dat", str(FIX / "test.dat"), "--libretro", str(FIX / "libretro"),
            "--psxcovers", str(FIX / "psxcovers"), "--disc-dir", str(FIX / "discs"),
            "--out", str(out)] + list(extra)
    o, e = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(o), contextlib.redirect_stderr(e):
        code = cp.main(argv)
    return code, o.getvalue(), e.getvalue()


def index(pack):
    rows = {}
    for line in (pack / "index.txt").read_text(encoding="utf-8").splitlines():
        f = line.split("\t")
        assert len(f) == 5, line
        rows[f[0]] = f
    return rows


def tree(root):
    return sorted(str(p.relative_to(root)).replace("\\", "/") for p in root.rglob("*") if p.is_file())


class CoverPackTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.out = pathlib.Path(self._tmp.name) / "out"
        self.pack = self.out / "covers"

    def tearDown(self):
        self._tmp.cleanup()

    # ---- names and serials
    def test_name_mapping(self):
        self.assertEqual(cp.libretro_name("Tomb Raider: Chronicles & More (Europe)"),
                         "Tomb Raider_ Chronicles _ More (Europe)")
        self.assertEqual(cp.libretro_name('a&b*c/d:e\\f`g<h>i?j|k'), "a_b_c_d_e_f_g_h_i_j_k")
        self.assertEqual(cp.libretro_name("Plain Name (USA)"), "Plain Name (USA)")
        # the mapped file is found by the exact tier (not only by the loose fallback)
        idx = cp.FileIndex(FIX / "libretro" / "Named_Boxarts", (".png",))
        path, tier = idx.find(cp.libretro_name("Tomb Raider: Chronicles & More (Europe)"))
        self.assertEqual(tier, "exact")
        self.assertEqual(path.read_bytes(), b"lib-png:SLES02197")

    def test_serial_normalisation(self):
        n = cp.normalise_serial
        self.assertEqual(n("SLUS-00506"), "SLUS00506")
        self.assertEqual(n("slus00506"), "SLUS00506")
        self.assertEqual(n(" SCUS_94900 "), "SCUS94900")
        self.assertIsNone(n("X"))
        self.assertIsNone(n(""))
        self.assertIsNone(n(None))
        self.assertEqual(cp.parse_serials("SCED-04009/PR"), ["SCED04009"])
        self.assertEqual(cp.parse_serials("SLPM-86001, SLPM-86002"), ["SLPM86001", "SLPM86002"])
        self.assertEqual(cp.parse_serials("SLES-12345; junk SLES-12346/ABC SLES-12345"),
                         ["SLES12345", "SLES12346"])
        self.assertEqual(cp.prefix_of("SLUS00506"), "SLUS")
        self.assertEqual(cp.prefix_of("PAPX90001"), "other")

    def test_dat_odd_entries(self):
        entries, stats = cp.read_dat(FIX / "test.dat")
        self.assertEqual(entries["SCED04009"], "Demo Disc Test (Europe)")      # suffix dropped
        self.assertEqual(entries["SLPM86001"], entries["SLPM86002"])           # two serials, one game
        self.assertEqual(stats["no_serial"], 2)                                # no serial + garbage
        self.assertEqual(stats["no_name"], 1)
        self.assertEqual(len(entries), 9)
        # truncated / garbage input does not raise
        games = cp.parse_dat('game ( name "x" serial "SLUS-00001" rom ( name')
        self.assertEqual(games[0]["serial"], ["SLUS-00001"])
        cp.parse_dat(")))( ( \" game")

    # ---- modes
    def test_jpg_first(self):
        code, _, err = run("--with-3d", out=self.out)
        self.assertEqual(code, 0, err)
        rows = index(self.pack)
        # psx-covers JPG wins although libretro has a PNG for the same game
        self.assertEqual(rows["SCUS94900"][1:], ["box/SCUS94900.jpg", "disc/SCUS94900.png", "psxcovers", "3d/SCUS94900.png"])
        self.assertEqual((self.pack / "box" / "SCUS94900.jpg").read_bytes(), b"psx-jpg:SCUS94900")
        # psx-covers lacks it: libretro PNG by mapped name
        self.assertEqual(rows["SLES02197"][1], "box/SLES02197.png")
        self.assertEqual(rows["SLES02197"][3], "libretro")
        self.assertEqual((self.pack / "box" / "SLES02197.png").read_bytes(), b"lib-png:SLES02197")
        # suffix serial reaches the psx-covers file; psx-only serial is in the pack
        self.assertEqual(rows["SCED04009"][1], "box/SCED04009.jpg")
        self.assertEqual(rows["SLUS00506"][1], "box/SLUS00506.jpg")
        # nothing anywhere: no index row, but listed in missing.txt
        self.assertNotIn("SLPS99999", rows)
        missing = (self.pack / "missing.txt").read_text(encoding="utf-8").splitlines()
        # the disc-dir-only serial has no box either, and no dat name
        self.assertEqual(missing, ["SLES55555\t-", "SLPS99999\tNothing Here (Japan)"])

    def test_png_first(self):
        code, _, err = run("--png-first", out=self.out)
        self.assertEqual(code, 0, err)
        rows = index(self.pack)
        self.assertEqual(rows["SCUS94900"][1], "box/SCUS94900.png")
        self.assertEqual(rows["SCUS94900"][3], "libretro")
        self.assertEqual((self.pack / "box" / "SCUS94900.png").read_bytes(), b"lib-png:SCUS94900")
        # libretro has nothing for this one: falls back to the psx-covers JPG
        self.assertEqual(rows["SCED04009"][1:4:2], ["box/SCED04009.jpg", "psxcovers"])
        self.assertEqual(rows["SLUS00506"][1], "box/SLUS00506.jpg")
        self.assertEqual(rows["SCUS94900"][4], "-")                            # no --with-3d

    def test_mode_flags_exclusive(self):
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                cp.parse_args(["--jpg-first", "--png-first"])

    # ---- multi-disc
    def test_multi_disc_shared_cover_and_per_disc_art(self):
        for flag, ext, src in (("--jpg-first", ".jpg", b"psx-jpg:SCUS94163"),
                               ("--png-first", ".png", b"lib-png:SCUS94163")):
            out = self.out / flag.strip("-")
            code, _, err = run(flag, out=out)
            self.assertEqual(code, 0, err)
            pack = out / "covers"
            rows = index(pack)
            for serial in ("SCUS94163", "SCUS94164", "SCUS94165"):     # one box row per disc, ONE file
                self.assertEqual(rows[serial][1], "box/SCUS94163" + ext)
            self.assertEqual((pack / "box" / ("SCUS94163" + ext)).read_bytes(), src)
            self.assertFalse((pack / "box" / ("SCUS94164" + ext)).exists())   # shared, not copied
            # disc art stays per disc: disc 1 png, disc 2 jpg, disc 3 none
            self.assertEqual(rows["SCUS94163"][2], "disc/SCUS94163.png")
            self.assertEqual((pack / "disc" / "SCUS94163.png").read_bytes(), b"disc:SCUS94163")
            self.assertEqual(rows["SCUS94164"][2], "disc/SCUS94164.jpg")
            self.assertEqual((pack / "disc" / "SCUS94164.jpg").read_bytes(), b"disc:SCUS94164")
            self.assertEqual(rows["SCUS94165"][2], "-")
        # a game with two serials in one dat entry shares too
        jp = self.out / "jpg-first" / "covers"
        self.assertEqual(index(jp)["SLPM86001"][1], index(jp)["SLPM86002"][1])
        self.assertEqual((jp / index(jp)["SLPM86001"][1]).read_bytes(), b"psx-jpg:SLPM86002")
        # --no-share: one file per serial again
        out = self.out / "noshare"
        code, _, err = run("--no-share", out=out)
        self.assertEqual(code, 0, err)
        rows = index(out / "covers")
        for serial in ("SCUS94163", "SCUS94164", "SCUS94165"):
            self.assertEqual(rows[serial][1], "box/" + serial + ".jpg")
            self.assertEqual((out / "covers" / "box" / (serial + ".jpg")).read_bytes(), b"psx-jpg:SCUS94163")

    def test_disc_art_by_serial_name(self):
        run(out=self.out)
        rows = index(self.pack)
        self.assertEqual(rows["SCUS94900"][2], "disc/SCUS94900.png")           # file named by serial

    def test_disc_dir_only_serial(self):
        # SLES-55555.png is in the disc dir only: not in the dat, not in psx-covers
        self.assertNotIn("SLES55555", cp.read_dat(FIX / "test.dat")[0])
        code, out, err = run(out=self.out)
        self.assertEqual(code, 0, err)
        rows = index(self.pack)
        self.assertEqual(rows["SLES55555"], ["SLES55555", "-", "disc/SLES55555.png", "-", "-"])
        self.assertEqual((self.pack / "disc" / "SLES55555.png").read_bytes(), b"disc:SLES55555")
        self.assertFalse((self.pack / "box" / "SLES55555.png").exists())
        self.assertIn("SLES55555\t-", (self.pack / "missing.txt").read_text(encoding="utf-8").splitlines())
        # a name that only looks like a serial ('FIFA 2000') does not make a pack row
        self.assertNotIn("FIFA2000", rows)
        self.assertEqual(sorted(r for r in rows), list(rows))                  # still sorted

    def test_disc_dir_unmatched_list(self):
        code, out, err = run("--report", out=self.out)
        self.assertEqual(code, 0, err)
        lines = (self.pack / "unmatched.txt").read_text(encoding="utf-8").splitlines()
        self.assertEqual(lines, ["FIFA 2000.png", "Mystery Disc.png"])
        self.assertIn("unmatched disc art (2", out)
        self.assertIn("Mystery Disc.png", (self.pack / "report.txt").read_text(encoding="utf-8"))
        # nothing unmatched is copied into the pack
        self.assertEqual([n for n in tree(self.pack) if "Mystery" in n or "FIFA" in n], [])
        # no disc dir, no unmatched.txt
        out2 = self.out.parent / "nodisc"
        argv = ["--dat", str(FIX / "test.dat"), "--psxcovers", str(FIX / "psxcovers"), "--out", str(out2)]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(cp.main(argv), 0)
        self.assertFalse((out2 / "covers" / "unmatched.txt").exists())
        self.assertNotIn("SLES55555", index(out2 / "covers"))

    def test_index_paths_exist_in_pack(self):
        run("--with-3d", out=self.out)
        for serial, f in index(self.pack).items():
            for col in (f[1], f[2], f[4]):
                if col != "-":
                    self.assertTrue((self.pack / col).is_file(), (serial, col))
                    # its own file, or the file of a serial with the same picture
                    owner = col.split("/")[1].split(".")[0]
                    self.assertEqual((self.pack / col).read_bytes(),
                                     (self.pack / col).read_bytes() if owner == serial else
                                     (self.pack / index(self.pack)[owner][[1, 2, 4][[f[1], f[2], f[4]].index(col)]]).read_bytes())

    # ---- output form
    def test_index_sorted_and_deterministic(self):
        run("--with-3d", "--report", out=self.out)
        lines = (self.pack / "index.txt").read_text(encoding="utf-8").splitlines()
        self.assertEqual([l.split("\t")[0] for l in lines], sorted(l.split("\t")[0] for l in lines))
        self.assertEqual(len(lines), 10)                                       # 9 + the disc-dir-only serial
        self.assertNotIn(b"\r", (self.pack / "index.txt").read_bytes())
        first = {n: (self.out / n).read_bytes() for n in tree(self.out)}
        out2 = self.out.parent / "out2"
        run("--with-3d", "--report", out=out2)
        self.assertEqual(tree(self.out), tree(out2))
        self.assertEqual(first, {n: (out2 / n).read_bytes() for n in tree(out2)})
        self.assertIn("SCUS", (self.pack / "report.txt").read_text(encoding="utf-8"))

    def test_report_table_and_root(self):
        code, out, _ = run("--root", ".", out=self.out)
        self.assertEqual(code, 0)
        self.assertTrue((self.out / "index.txt").is_file())
        for prefix in cp.PREFIXES + ("other", "TOTAL"):
            self.assertIn(prefix, out)

    def test_dry_run_writes_nothing(self):
        code, out, _ = run("--dry-run", "--with-3d", "--report", out=self.out)
        self.assertEqual(code, 0)
        self.assertFalse(self.out.exists())
        self.assertIn("dry run", out)

    # ---- errors
    def test_missing_source_dir(self):
        for flag in ("--libretro", "--psxcovers", "--disc-dir"):
            argv = ["--dat", str(FIX / "test.dat"), "--libretro", str(FIX / "libretro"),
                    "--out", str(self.out)]
            argv += [flag, str(FIX / "does_not_exist")]
            o, e = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(o), contextlib.redirect_stderr(e):
                code = cp.main(argv)
            self.assertEqual(code, 2, flag)
            self.assertIn("not a directory", e.getvalue())
            self.assertFalse(self.out.exists())

    def test_missing_dat_and_bad_combinations(self):
        for argv in (["--dat", str(FIX / "nope.dat"), "--psxcovers", str(FIX / "psxcovers")],
                     ["--libretro", str(FIX / "libretro")],            # libretro without dat
                     ["--dat", str(FIX / "test.dat")],                 # no image source
                     ["--psxcovers", str(FIX / "psxcovers"), "--with-3d", "--root", "../x"]):
            e = io.StringIO()
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(e):
                code = cp.main(argv + ["--out", str(self.out)])
            self.assertEqual(code, 2, argv)
            self.assertIn("error", e.getvalue())
        self.assertFalse(self.out.exists())

    def test_copy_is_byte_identical(self):
        run(out=self.out)
        src = (FIX / "psxcovers" / "covers" / "default" / "SCUS-94900.jpg").read_bytes()
        self.assertEqual((self.pack / "box" / "SCUS94900.jpg").read_bytes(), src)

    # ---- Pillow (skipped when it is not installed)
    def test_resize_without_pillow_is_a_clear_error(self):
        try:
            import PIL  # noqa: F401
            self.skipTest("Pillow is installed")
        except ImportError:
            pass
        e = io.StringIO()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(e):
            code = cp.main(["--psxcovers", str(FIX / "psxcovers"), "--resize", "64", "--out", str(self.out)])
        self.assertEqual(code, 2)
        self.assertIn("Pillow", e.getvalue())
        self.assertFalse(self.out.exists())

    def test_resize_with_pillow(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow is not installed")
        src = pathlib.Path(self._tmp.name) / "src"
        (src / "covers" / "default").mkdir(parents=True)
        Image.new("RGB", (400, 200), (200, 10, 10)).save(src / "covers" / "default" / "SLUS-00001.jpg")
        Image.new("RGB", (40, 20), (10, 10, 200)).save(src / "covers" / "default" / "SLUS-00002.jpg")
        with contextlib.redirect_stdout(io.StringIO()):
            code = cp.main(["--psxcovers", str(src), "--resize", "100", "--out", str(self.out)])
        self.assertEqual(code, 0)
        with Image.open(self.pack / "box" / "SLUS00001.jpg") as im:
            self.assertEqual(im.size, (100, 50))
        self.assertEqual((self.pack / "box" / "SLUS00002.jpg").read_bytes(),          # never upscaled
                         (src / "covers" / "default" / "SLUS-00002.jpg").read_bytes())


class GamesTest(unittest.TestCase):
    def rows(self):
        R = cp.Row
        return [R("SLUS00506", "Frogger (USA)", None, None, None),
                R("SCUS94163", "Final Fantasy VII (USA) (Disc 1)", None, None, None),
                R("SCUS94164", "Final Fantasy VII (USA) (Disc 2)", None, None, None),
                R("SCUS94165", "Final Fantasy VII (USA) (Disc 3)", None, None, None),
                R("SLUS00530", "Gex (USA)", None, None, None)]

    def test_select(self):
        names = ["Frogger (USA)",                         # exact
                 "final fantasy vii (usa)",               # multi-disc PBP: every disc
                 "Gex (USA) (Track 1)",                   # a .bin track
                 "Nothing Like It (Europe)"]              # no match
        rows, unmatched = cp.select_games(self.rows(), names)
        self.assertEqual(sorted(r.serial for r in rows),
                         ["SCUS94163", "SCUS94164", "SCUS94165", "SLUS00506", "SLUS00530"])
        self.assertEqual(unmatched, ["Nothing Like It (Europe)"])

    def test_one_disc_only(self):
        rows, _ = cp.select_games(self.rows(), ["Final Fantasy VII (USA) (Disc 2)"])
        self.assertEqual([r.serial for r in rows], ["SCUS94164"])

    def test_bracket_serial_rev_and_typo(self):
        R = cp.Row
        rows = self.rows() + [R("SCUS94244", "Crash Bandicoot - Warped (USA)", None, None, None),
                              R("SCUS94467", "Spyro - Year of the Dragon (USA) (v1.1)", None, None, None)]
        got, unmatched = cp.select_games(rows, [
            "Ape Escape [U] [SLUS-00530]",                  # a serial in brackets, at the end
            "Crash Bandicoot 3 - Warped [U] [SLUS-94244]",  # a wrong prefix: the number decides
            "Spyro - Year of the Dragon (USA) (Rev 1)",     # rev/version tags on both sides ignored
            "Spyro 2"])                                     # too vague: nothing
        self.assertEqual(sorted(r.serial for r in got), ["SCUS94244", "SCUS94467", "SLUS00530"])
        self.assertEqual(unmatched, ["Spyro 2"])

    def test_serial_named_file(self):
        rows, _ = cp.select_games(self.rows(), ["SLUS-00506"])
        self.assertEqual([r.serial for r in rows], ["SLUS00506"])

    def test_names_from_folder_and_list(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            (d / "sub").mkdir()
            (d / "Frogger (USA).cue").write_text("x")
            (d / "sub" / "Gex (USA) (Track 1).bin").write_text("x")
            (d / "readme.txt").write_text("x")                  # not a game file
            self.assertEqual(cp.game_names(d), ["Frogger (USA)", "Gex (USA) (Track 1)"])
            lst = d / "list.txt"
            lst.write_text("# card listing\nsd:/wiistation/isos/Frogger (USA).cue\nFinal Fantasy VII (USA).pbp\n\n")
            self.assertEqual(cp.game_names(lst), ["Final Fantasy VII (USA)", "Frogger (USA)"])


class ResolveStubTest(unittest.TestCase):
    """libretro-thumbnails' aliases are git symlinks; Windows turns them into tiny text files."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.d = pathlib.Path(self._tmp.name)
        (self.d / "Real (USA).png").write_bytes(b"\x89PNG\r\n\x1a\n" + b"x" * 600)

    def tearDown(self):
        self._tmp.cleanup()

    def test_real_image_is_kept(self):
        p = self.d / "Real (USA).png"
        self.assertEqual(cp.resolve_stub(p), p)

    def test_stub_resolves_to_target(self):
        (self.d / "Alias.png").write_text("Real (USA).png")
        self.assertEqual(cp.resolve_stub(self.d / "Alias.png"), self.d / "Real (USA).png")

    def test_chain_of_stubs(self):
        (self.d / "A.png").write_text("B.png")
        (self.d / "B.png").write_text("Real (USA).png\n")
        self.assertEqual(cp.resolve_stub(self.d / "A.png"), self.d / "Real (USA).png")

    def test_broken_stub_is_dropped(self):
        (self.d / "Gone.png").write_text("Nothing here.png")
        self.assertIsNone(cp.resolve_stub(self.d / "Gone.png"))

    def test_loop_is_dropped(self):
        (self.d / "A.png").write_text("B.png")
        (self.d / "B.png").write_text("A.png")
        self.assertIsNone(cp.resolve_stub(self.d / "A.png"))

    def test_small_real_jpeg_is_kept(self):
        (self.d / "tiny.jpg").write_bytes(b"\xff\xd8\xff\xe0" + b"x" * 20)
        self.assertEqual(cp.resolve_stub(self.d / "tiny.jpg"), self.d / "tiny.jpg")

    def test_small_non_pointer_file_is_kept(self):
        (self.d / "odd.png").write_bytes(b"not a pointer, just bytes\nwith two lines")
        self.assertEqual(cp.resolve_stub(self.d / "odd.png"), self.d / "odd.png")

    def test_index_uses_the_target_picture_under_the_alias_name(self):
        (self.d / "Alias.png").write_text("Real (USA).png")
        (self.d / "Gone.png").write_text("Nothing here.png")
        idx = cp.FileIndex(self.d)
        path, tier = idx.find("Alias")
        self.assertEqual(path, self.d / "Real (USA).png")
        self.assertIsNone(idx.find("Gone")[0])


class UnstubTest(unittest.TestCase):
    def test_stubs_become_pictures(self):
        import cover_unstub as cu
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            png = b"\x89PNG\r\n\x1a\n" + b"x" * 600
            (d / "Real.png").write_bytes(png)
            (d / "Alias.png").write_text("Real.png")
            (d / "Alias2.png").write_text("Alias.png")
            (d / "Gone.png").write_text("Missing.png")
            fixed, broken, seen, how = cu.unstub(d, dry_run=True)
            self.assertEqual((fixed, broken, seen), (2, ["Gone.png"], 4))
            self.assertEqual((d / "Alias.png").read_text(), "Real.png")          # a dry run changes nothing
            fixed, broken, seen, how = cu.unstub(d)
            self.assertEqual((fixed, broken, seen), (2, ["Gone.png"], 4))
            self.assertEqual(how, {"link": 2, "copy": 0})
            self.assertEqual((d / "Alias.png").read_bytes(), png)
            self.assertEqual((d / "Alias2.png").read_bytes(), png)
            self.assertTrue(os.path.samefile(d / "Alias.png", d / "Real.png"))      # a hard link, no extra space
            self.assertEqual(cu.unstub(d)[0], 0)                                     # a second run has nothing to do
            self.assertEqual((d / "Gone.png").read_text(), "Missing.png")          # a broken one stays


if __name__ == "__main__":
    unittest.main(verbosity=2)
