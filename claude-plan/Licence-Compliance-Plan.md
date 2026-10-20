# SnapCap — Licence Compliance Plan

**Owner of everything SnapCap must do because it *distributes other people's code*.** Before this
file existed the subject was split in two — `Implementation-Plan.md` Stage 7 held the Qt half,
the recording plan the FFmpeg half — and neither owned the question they share: *what must be
true before a public ZIP goes out.* Stage 7 remains the record of how Qt's obligation **arose**;
the recording plan was retired 2026-10-20 and the FFmpeg half of that history now lives in §1.3.
This file is the record of **how both get discharged**, in what order, and what stays true
afterwards.

**Status (2026-08-04, updated same day):** **both source-hosting duties are discharged.** FFmpeg's
and Qt's Corresponding Source are published in a repo we own, both URLs point at them, and
`-DSNAPCAP_RELEASE=ON` passes for the first time — verified by running the gates in both directions
(§4 Steps 1–4 below). Nothing has been conveyed yet, so nothing was ever out of compliance; what
changed is that a compliant release is now *possible*, where before it was blocked. One patent
question is documented and deliberately parked (§5).

**O3 is cleared (2026-08-07)** — the FFmpeg source bundle deferred on 2026-08-04 has been
regenerated and re-uploaded, and the published assets now describe the DLLs we actually ship (§2).

Remaining before a public binary: the app repo does not exist yet (features first), so §4 Steps 5–7
wait on that. Nothing else is outstanding.

---

## 1. What SnapCap conveys

"Convey" is the operative verb throughout: obligations attach at the moment a binary reaches
someone else, not when it is built. A ZIP sitting in `build/package/` triggers nothing.

| Shipped in the ZIP | Licence | What that obliges |
|---|---|---|
| **Qt 6.11.1 DLLs** (+ `platforms/`, `styles/`, `tls/`, `imageformats/`) | LGPLv3 | Dynamic + replaceable ✅; notices ✅; **Corresponding Source hosted by us** ✅ (`qt-6.11.1` release, 2026-08-04) |
| **FFmpeg DLLs** — `avcodec-62`, `avformat-62`, `avutil-60`, `avfilter-11`, `avdevice-62`, `swscale-9`, `swresample-6` (24.9 MB) | LGPLv3 (`--enable-version3`) | Dynamic + replaceable ✅; notices ✅; **Corresponding Source hosted by us** ✅ (current as of the 2026-10-20 `--disable-iconv` rebuild — O4/O5, §2) |
| Libraries statically linked **into** those FFmpeg DLLs | all permissive — see §1.1 | Attribution only ✅ |
| `eng.traineddata` (OCR) | Apache-2.0 | Attribution ✅ |
| yaml-cpp, ZXing | permissive | Attribution ✅ |
| **`SnapCap.exe` itself** | ours | LGPLv3 **§3** — see §1.2 |

### 1.1 The FFmpeg sub-libraries are all permissive — this is the whole reason the job is small

Because we build FFmpeg ourselves (`tools/build_ffmpeg.sh`) with `--disable-everything` plus only
what SnapCap calls, exactly seven libraries are linked in, and **none is copyleft**:

| Library | Licence | Role |
|---|---|---|
| libopenh264 (Cisco) | BSD-2 | H.264 encoding |
| kvazaar (Tampere Univ.) | BSD-3 | H.265/HEVC encoding |
| SVT-AV1 | BSD-3 + AOM Patent License 1.0 | AV1 encoding |
| libvpx (Google) | BSD-3 | VP9 encoding |
| Opus (Xiph.Org) | BSD-3 | Opus audio |
| **dav1d (VideoLAN)** | **BSD-2** | **AV1 *decoding* — added 2026-08-04. Not optional: FFmpeg's native `av1` decoder returns `ENOSYS` without a hwaccel, so AV1 recordings were unplayable without it.** |
| libvpl (Intel) | MIT | Intel QSV dispatcher |

So the source-offer obligation is **FFmpeg alone**. Contrast the BtbN prebuilt this replaced:
~50 statically-linked libraries, over a dozen copyleft, each needing its own hosted source. That
gap — one tarball versus a ~50-library audit — is the single biggest reason P1 (build it ourselves)
was worth the effort, and it is why re-opening "should we ship FFmpeg at all?" now buys very little.

*(Two earlier claims in the recording plan were wrong and are corrected here for the record:
kvazaar is **BSD-3, not LGPL** — it relicensed at 2.0, and this pin is 2.3.2 — and it was never
"the one LGPL sub-library" of the BtbN build, where it was one of many.)*

### 1.2 The exe is not obligation-free even if FFmpeg were dropped

`SnapCap.exe` is compiled against Qt and FFmpeg **headers**, which contain inline functions and
struct layouts. LGPLv3 **§3** ("Object Code Incorporating Material from Library Header Files")
governs exactly this and permits conveying such object code under terms of our choice, provided we
give notice that the library is used and is covered by the LGPL, and supply a copy of the licence.
`LICENSE.LGPLv3.txt` + `THIRD_PARTY_NOTICES.txt` already ship beside the exe, so **this is
satisfied** — but it is worth knowing the floor is not zero. Removing FFmpeg from the ZIP would not
make SnapCap a licence-free artifact; Qt alone keeps §3 and §4 live.

### 1.3 How the stripped build came about, and the traps it hit

*(Folded in 2026-10-20 from the retired recording plan.)* Recording first ran on BtbN's pinned
`win64-lgpl-shared` prebuilt — 136 MB of DLLs, taking the ZIP from 32 MB to ~90 MB, with the
copyleft surface §1.1 describes. `tools/build_ffmpeg.{ps1,sh}` replaced it: same MinGW kit SnapCap
links with (MSYS2 supplies only make/nasm/pkg-config), `--disable-everything` plus what SnapCap
calls, **24.7 MB** of DLLs and a **42.5 MB** ZIP.

The decisions behind it (user, 2026-08-04), labelled as this file cites them: **P1** build FFmpeg
ourselves, stripped; **P2** ship the DLLs in the ZIP (unzip-and-run, no first-use download);
**P3** self-host the Corresponding Source in a repo we own; **P4** defer Qt's source — since closed
(§2).

**`--enable-version3` is kept on purpose.** No dependency needs it, but it makes the result
LGPLv3, which the `LICENSE.LGPLv3.txt` already beside the exe covers; without it the result is
LGPLv2.1 and would need a second licence file for no benefit.

**Every omission fails at runtime, never at build time.** The traps so far, numbered because §2
and §4 refer back to them:

1. **Hardware encoders silently vanished** — the first build left out `--enable-nvenc/amf/libvpl`
   and the `*_mf` encoder names, so every codec quietly fell back to software; only the log said so.
2. **NVENC compiled but would not open** — nv-codec-headers 13.1 declares an API newer than
   current drivers support. Pinned to n12.2.72.0.
3. **`configure-line.txt` was written empty** (a wrong grep) — a bundle missing a part the licence
   requires. Now taken from `config.log`'s first line, with a warning if it comes out empty.
4. **No `gif` parser** — GIF recordings decoded to a green screen in the preview player.
5. **No `libdav1d`** — FFmpeg's native AV1 decoder has no software path, so AV1 recordings were
   unplayable.

Traps 4–5 got past every capture/encode check because only the preview player decodes. After any
change to the build, re-check: all four codecs (H.264/H.265/AV1/VP9) recording with the expected
encoder, the GIF chain, AAC and Opus audio, pause, both grabbers (`ddagrab` and `gdigrab`), player
seek on every output format, and trim.

---

## 2. Current state

**Already satisfied, and structurally rather than by memory:**

- **Dynamic linking + replaceability.** Both Qt and FFmpeg ship as DLLs beside the exe; a user can
  substitute a compatible build by copying files. This is what LGPLv3 §4(d)(0) asks for, and the
  portable-ZIP-no-installer decision is what makes it true by construction.
- **Notices.** `LICENSE.LGPLv3.txt`, `LICENSE.GPLv3.txt` and `THIRD_PARTY_NOTICES.txt` ship at the
  repo root *and* beside the exe. The FFmpeg entry names all seven DLLs, the exact commit, the
  `--enable-version3` reasoning, and the seven sub-libraries — whose full texts, and those of the
third-party code inside Qt, it carries since 2026-10-20 (see O4).
- **Preferences → About** exposes version, the Qt/LGPLv3 statement, **both** source links, and
  three buttons opening the licence documents.
- **A packaging gate.** `option(SNAPCAP_RELEASE)` in `CMakeLists.txt` runs two `install(CODE)`
  checks at install time: `WARNING` on a normal `--target package` (so clean-VM test ZIPs still
  build), `FATAL_ERROR` under `-DSNAPCAP_RELEASE=ON`. They are **separate checks on purpose** —
  fixing one must not mask the other.

  | Gate | Fires while |
  |---|---|
  | Qt | `SNAPCAP_QT_SOURCE_URL` matches `download\.qt\.io` |
  | FFmpeg | `SNAPCAP_FFMPEG_SOURCE_URL` does **not** match `/releases/(tag\|download)/` |

**Both former deferrals are now closed** (2026-08-04). The source lives in
**`github.com/smolsquirrel936/SnapCap-thirdparty-src`**, a public repo we own, as two Releases:

| Release | Assets | Verified |
|---|---|---|
| `ffmpeg-8c9502e9b0` | source tarball 16,902,915 B; `build_ffmpeg.sh` **16,070 B**; `build_ffmpeg.ps1` **3,996 B**; `configure-line.txt` **1,490 B**; `README.md` **1,947 B** | every asset byte-for-byte against disk, all `state: uploaded` (four sizes refreshed 2026-08-07 clearing O3; `build_ffmpeg.sh` + `configure-line.txt` again 2026-10-20 for `--disable-iconv`, see O4, then both scripts once more the same day for O5; the tarball is the original upload) |
| `qt-6.11.1` | `qt-everywhere-src-6.11.1.tar.xz` 1,017,723,080 B | upstream MD5 `25d4d1dd…5985` checked before upload; GitHub size matches disk exactly |

The FFmpeg bundle is assembled reproducibly by `tools/fetch_ffmpeg_source.ps1`. Its scripts are
**not optional padding**: GPLv3 defines Corresponding Source to include "the scripts used to
control compilation and installation", so a bare tarball would not satisfy it. `configure-line.txt`
was checked non-empty both on disk and *in the uploaded asset* — it silently shipped empty once
before (§1.3 trap 3).

Qt's release is upstream's archive unmodified, because we do not compile Qt — see §4 Step 3.

### ✅ O3 — the published FFmpeg source was stale (deferred 2026-08-04, cleared 2026-08-07)

The FFmpeg DLLs were **rebuilt on 2026-08-04** to add the `gif` parser and `libdav1d`
(§1.3 traps 4–5 — without them GIF and AV1 recordings could not be played back at all).
That changed the binaries, the configure line, and the dependency count, which left the published
release describing a build we no longer shipped. Nothing was ever out of compliance — no binary had
been conveyed — and the release tag stayed correct, since the FFmpeg commit did not move; only our
build of it did.

**Cleared 2026-08-07** by regenerating the bundle and replacing **four** of the five assets. The
tarball was deliberately left alone (same commit, so re-uploading 16 MB would prove nothing). Sizes
and fresh `updated_at` stamps verified against the REST API afterwards:

| Asset | Before | After |
|---|---|---|
| `build_ffmpeg.sh` | 13,038 B | **15,398 B** |
| `build_ffmpeg.ps1` | 3,111 B | **3,986 B** |
| `configure-line.txt` | 1,442 B | **1,474 B** (non-empty — the §1.3 trap-3 check) |
| `README.md` | 1,709 B | **1,947 B** (1,801 at the first pass, then again for the meson/ninja prerequisites — see below) |

All three build files were confirmed **SHA-256 identical** to their in-repo sources before upload
(`tools/build_ffmpeg.sh`, `tools/build_ffmpeg.ps1`, `third_party/ffmpeg/configure-line.txt`), and
the published `README.md` was **downloaded and read back** rather than trusted by size.

**A fourth staleness surfaced while clearing it, and it was not in this file's ledger.** The bundle
`README.md` is *generated* by `tools/fetch_ffmpeg_source.ps1` from a here-string, and that template
still listed **six** statically linked libraries — dav1d was never added when the build gained it.
Regenerating therefore produced a byte-identical 1,709 B file and would have "refreshed" nothing
while leaving a false claim about the binaries in the bundle. The template is fixed. **The lesson
generalises: when a dependency is added, the generator's prose needs the same edit as
`THIRD_PARTY_NOTICES.txt`, and only reading the output catches it.**

**A fifth, found by re-reading the same template (2026-08-07).** Its rebuild instructions said
"install MSYS2 with make/nasm/pkg-config/git" — but dav1d is built with **meson**, so the build now
also needs meson (`pip install meson`, not an MSYS2 package) and ninja (from the Qt tools), and
`build_ffmpeg.ps1` throws on either being missing. Anyone following the published instructions
could not have reproduced the binaries, which is the one thing Corresponding Source exists to
allow. Fixed in the template and re-uploaded; `README.md` is **1,947 B** as published.

**The release *description* was stale too, and is now fixed** (2026-08-07). It is prose, **not**
Corresponding Source, so it gated nothing legally — but it claimed "23 MB" and "Six libraries" with
no dav1d, which was untrue of the binaries. Replaced from the disk copy and verified on the
rendered page. Two things about it are worth carrying forward: the body is kept on disk as
`SnapCap-thirdparty-src/ffmpeg-8c9502e9b0/notes.md`, which the generator script never touches (it
is hand-maintained, unlike the generated `README.md`); and a release body lives on the release
object, so **no commit or push can update it** — only the release editor or the API.

**The hazard this whole item illustrates, which has not gone away:** the packaging gate matches
`SNAPCAP_FFMPEG_SOURCE_URL` against `/releases/(tag|download)/`. It validates the URL's *shape*,
not that the assets behind it describe the DLLs being shipped. A stale release passes silently.
**Any future FFmpeg rebuild re-opens this**: re-run `fetch_ffmpeg_source.ps1`, then replace the
assets, before any ZIP carrying the new DLLs is distributed.

**Why this mattered for Qt specifically**, since the old link was lawful and it would be easy to
misread this as fixing a violation: GPLv3 §6(d) permits third-party hosting, so pointing at
`download.qt.io` broke nothing. What §6(d) does not do is move the duty, which closes with *"you
remain obligated to ensure that it is available for as long as needed."* Compliance rested on The
Qt Company's retention policy for 6.11.1 — which we neither control nor get told about. Now it
doesn't.

### ✅ O4 — notices audit before opening the repo (opened and cleared 2026-10-20)

An audit of every redistributed binary against `THIRD_PARTY_NOTICES.txt` found the file named
components without carrying the texts their licences require. **Fixed 2026-10-20:**

- **FFmpeg's seven static libraries** were listed by name only. BSD clause 2 / MIT require the
  copyright line *and* licence text with binaries. Now reproduced verbatim from
  `third_party/ffmpeg-build/*/` in **Appendix A**, plus nv-codec-headers and the AMF headers (MIT,
  compiled into `avcodec`). SVT-AV1 is **BSD-3-Clause-Clear**, not plain BSD-3, and its **AOM Patent
  License 1.0 explicitly requires its own text in binary distributions** — included.
- **Qt's own bundled third-party code** was not mentioned, and the file falsely said md4c was not
  redistributed (it is in `Qt6Gui.dll`). **Appendix B** now carries 45 components, taken from Qt
  6.11.1's `qt_attribution.json` (qtbase/qtsvg/qtimageformats at tag `v6.11.1`), filtered to what
  the shipped Windows modules and plugins use, with the two mandatory documentation statements
  (FreeType credit, IJG) in the Qt section. The plugin list gained `qjpeg`/`qgif`/`qico`.
- **winpthreads, Mesa and LLVM** texts were missing. `opengl32sw.dll` is Mesa **11.2.2** with
  **LLVM 3.6.2** (NCSA) inside, identified from strings in the DLL.

- **GNU libiconv (LGPL-2.1+) was statically linked into `avcodec-62.dll`**, in no notice and not in
  the published source bundle. FFmpeg's configure probes libc iconv *even under*
  `--disable-autodetect` (only `--disable-iconv` stops it), and the Qt MinGW kit ships
  `libiconv.a`. Evidence: `CONFIG_ICONV 1`, `libc_iconv=yes` in `config.log`, libiconv's alias
  table in the DLL. Its only use is subtitle charset recoding (`libavcodec/decode.c`), which no
  enabled component reaches. **Rebuilt 2026-10-20 with `--disable-iconv`** (`tools/build_ffmpeg.sh`):
  `CONFIG_ICONV 0`, no libiconv strings left, `avcodec` 21.0 → 20.1 MB; configure line differs by
  that flag alone; all seven DLLs export identical symbol sets and the headers are unchanged, so
  SnapCap needed no code change. Earlier test ZIPs, never made public, predated this and carried
  libiconv; the last of them was deleted on 2026-10-20, so no libiconv build remains available.
- **SnapCap's own code is now MIT** (`LICENSE.txt`, 2026-10-20). That filename is deliberate:
  GitHub's licensee scores only `LICENSE`/`LICENSE.<ext>` names, and the third-party
  `LICENSE.<name>.txt` files score **zero** (a second dot defeats every pattern), so `LICENSE.txt`
  is the one file it detects. Ships beside the exe via the same `install(FILES)` + POST_BUILD copy.

**The re-upload the rebuild required is done (2026-10-20).** `fetch_ffmpeg_source.ps1` regenerated
the bundle; only **two** assets differed from the release, by the `--disable-iconv` flag alone:
`build_ffmpeg.sh` 15,398 → **15,754 B** and `configure-line.txt` 1,474 → **1,490 B**. Both were
replaced (`gh release upload --clobber`), then **downloaded back and `cmp`'d byte-identical** to
`tools/build_ffmpeg.sh` and `third_party/ffmpeg/configure-line.txt`. `build_ffmpeg.ps1` and the
generated `README.md` came out byte-identical, so they were left alone, as was the tarball (same
commit). The release body (`notes.md`) needed no edit: its "25 MB" still holds (24.9 MB now).

### ✅ O5 — build-script comments changed after the O4 upload (opened and cleared 2026-10-20)

Retiring the recording plan repointed the comments in `tools/build_ffmpeg.sh` and
`tools/build_ffmpeg.ps1` that cited it, and added a note on the `gif` parser to the `.sh`.
**No build behaviour changed** — but both files are published Corresponding Source assets, so
the `ffmpeg-8c9502e9b0` release no longer matches them byte for byte. Clear it the way O4 was
cleared: re-run `tools/fetch_ffmpeg_source.ps1`, replace `build_ffmpeg.sh` and
`build_ffmpeg.ps1` (`gh release upload --clobber`), download them back and `cmp` against
`tools/`. `configure-line.txt` and the generated `README.md` should come out unchanged — confirm
that rather than assume it.

**Cleared the same day.** The user regenerated the bundle; the release was first diffed against
`tools/` — exactly the two scripts differed, by comment lines only — then both were replaced with
`gh release upload --clobber` (`build_ffmpeg.sh` 15,754 → **16,070 B**, `build_ffmpeg.ps1`
3,986 → **3,996 B**), downloaded back and `cmp`'d **byte-identical** to `tools/`.
`configure-line.txt` was downloaded and `cmp`'d identical to `third_party/ffmpeg/` and left alone.
`README.md` (1,947 B) and the tarball were not re-uploaded: the README template and its inputs
(FFmpeg pin, SnapCap version) did not change, and neither did the commit.

---

## 3. The decision: keep shipping FFmpeg (P2 stands)

Reconsidered 2026-08-04 on licence grounds specifically, and **unchanged**. The reasoning, so it
does not have to be re-derived:

1. **Qt makes the infrastructure mandatory anyway.** SnapCap ships Qt DLLs under identical LGPLv3
   terms, and `qt-everywhere-src` (~1 GB) dwarfs FFmpeg's 16.9 MB tarball. A hosting repo must
   exist regardless. Once it does, FFmpeg's marginal cost is *one extra release asset*.
2. **Dropping FFmpeg from the ZIP removes an instance, not a class.** It would leave §3 and Qt's
   §4 fully in force — a smaller job, not a different one.
3. **The FFmpeg obligation has already collapsed to near-zero.** One tarball, no copyleft
   dependencies, bundle assembled, gate wired. The expensive version of this problem was BtbN's,
   and P1 already eliminated it.
4. **The alternative costs product quality.** A user-supplied FFmpeg must be a *shared, LGPL,
   major-version-exact* build (`avcodec-62` etc.); anything else fails silently or, in the case of
   BtbN's GPL variant, creates a **worse** licence position than the one we started from — a GPL
   library inside a non-GPL app, invited by our own instructions.

### 3.1 Rejected, with reasons (do not re-litigate on licence grounds)

- **User downloads FFmpeg themselves.** Genuinely removes the FFmpeg source duty — but see 1–4
  above, and note it forfeits unzip-and-run to save 24.7 MB while asking users to fetch BtbN's
  136 MB general-purpose build — which, being general-purpose, would at least have shipped the
  `gif` parser and dav1d that our stripped build had to learn to include.
- **In-app first-run downloader.** Same benefit, weaker story: an app that fetches the binary is
  more entangled than one that never touches it. Not worth the code.

### 3.2 One adjacent change that is *not* a licence measure

Today FFmpeg is **load-time linked** (`target_link_libraries` against `libav*.dll.a`), so absent
DLLs kill the process in the loader **before `main()`** — which is why the `install(FILES …)` rule
exists. Splitting the FFmpeg-touching code (`WinRecorder`, `platform/media/*`) into a plugin DLL
loaded via `LoadLibrary` would degrade that to "recording disabled" instead. The interfaces for it
already exist — `IRecorder`, `createMediaPlayer()`, `trimRecording()` — and `Platform::recorder` is
already documented null-able with the Record button handling it.

**That is a robustness improvement, and it is filed as one.** It is listed here only to record that
it was considered and rejected *as a compliance measure*: it changes nothing about what we convey.

---

## 4. Execution plan

Ordered. Step 6's ordering rule is the one that cannot be recovered from if broken.

### Step 1 — Create the third-party source repo ✅ done 2026-08-04
Public GitHub repo `SnapCap-thirdparty-src`, separate from the app repo so GB-scale archives
never enter the app repo's clone path, and so each dependency version is uploaded **once** rather
than re-attached to every app release.
→ *Verify:* repo exists, public, and its URL matches the `SNAPCAP_FFMPEG_SOURCE_URL` default host.

### Step 2 — Publish the FFmpeg source ✅ done 2026-08-04
Re-run `tools\fetch_ffmpeg_source.ps1` (cheap, and confirms reproducibility), then publish the
five files of `ffmpeg-8c9502e9b0/` as a Release tagged **`ffmpeg-8c9502e9b0`** — tagged for the
*dependency* version, not the SnapCap version, since one upload serves many app releases.
→ *Verify:* `configure-line.txt` is **non-empty** in the uploaded asset. It was silently written
empty once (§1.3 trap 3) — that alone would ship a bundle missing a part the licence requires.

### Step 3 — Publish the Qt source ✅ done 2026-08-04
Download `qt-everywhere-src-6.11.1` matching the kit actually built against — the **full** archive,
not a per-module subset. Attach it to the same `SnapCap-thirdparty-src` repo, tagged `qt-6.11.1`.
→ *Verify:* actual size against GitHub's 2 GB per-asset cap before uploading (~1 GB expected).

### Step 4 — Repoint both URLs ✅ done 2026-08-04
Edit the `set()` **defaults** in `CMakeLists.txt` — not a `-D` override — so git records what
shipped:
- `SNAPCAP_FFMPEG_SOURCE_URL` → the `ffmpeg-8c9502e9b0` release page.
- `SNAPCAP_QT_SOURCE_URL` → the `qt-6.11.1` release page. This also retires the
  `${Qt6_VERSION}`-derived interim guard, which was only ever a stopgap against kit bumps.
→ *Verify:* both gates pass under `-DSNAPCAP_RELEASE=ON` (Step 5).

#### Steps 1–4 results (2026-08-04)

Both gates were exercised **in both directions**, because a gate that has never fired proves
nothing:

| Configuration | `cmake --install` | Expected |
|---|---|---|
| Both URLs published, `SNAPCAP_RELEASE=OFF` | exit 0, no output | silent ✅ |
| FFmpeg URL reverted to the bare repo, `RELEASE=ON` | **exit 1**, `CMake Error … does not point at a published release` | fires ✅ |
| Both URLs published, `RELEASE=ON` | **exit 0, clean** | passes ✅ |

The third row is the milestone: `-DSNAPCAP_RELEASE=ON` had never once succeeded before. Verified
afterwards by byte-searching `build/SnapCap.exe` — both release URLs are present and
`download.qt.io` no longer appears anywhere in the binary. (`findstr` reports a false negative on
the exe; it mishandles binaries with very long lines. Search the bytes.)

**Two traps found doing this, both worth knowing before touching these variables:**

1. **`SNAPCAP_FFMPEG_SOURCE_URL` is a `CACHE STRING`.** Editing the `set()` default does **not**
   change an existing build tree — `build/CMakeCache.txt` kept the old placeholder through a
   reconfigure. Pass `-DSNAPCAP_FFMPEG_SOURCE_URL=…` or delete the cache entry. `SNAPCAP_QT_SOURCE_URL`
   is a plain variable and has no such problem. This asymmetry is now noted at both definitions.
2. **`SNAPCAP_RELEASE` persists in the cache too.** Testing with `-DSNAPCAP_RELEASE=ON` leaves every
   later `--target package` fatal until explicitly set back `OFF`. It was restored to `OFF`.

### Step 5 — Build the release artifact
```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSNAPCAP_RELEASE=ON `
      -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 -DCMAKE_CXX_COMPILER=g++ -DSNAPCAP_FETCH_YAMLCPP=ON
cmake --build build --target package
```
→ *Verify:* it **completes**. Both gates are `FATAL_ERROR` here, so success *is* the check. Confirm
first that it still **fails** with one URL reverted — a gate that never fires proves nothing.

### Step 6 — Publish, in this order
Attach `SnapCap-<version>-win64.zip` to the SnapCap release. **The source must already be live**
(Steps 2–3). The obligation attaches at conveyance; uploading source afterwards does not cure a
release that already went out.
→ *Verify:* both source links resolve from a logged-out browser before the ZIP link is shared.

### Step 7 — Post-publish check on the shipped artifact
Unzip the published ZIP on a clean VM and, in the running app, click all three licence buttons and
**both** source links in Preferences → About.
→ *Verify:* `kDocs` filenames still agree with the shipped `.txt` files. Nothing fails the build if
they drift — the buttons just go silent — and the `.txt` extensions must survive, or Windows shows
its "how do you want to open this file?" picker instead of the text viewer.

### Ongoing — retention
`SnapCap-thirdparty-src` **must outlive every binary that points at it**. Deleting it later
retroactively breaks compliance for ZIPs already distributed. This is the only duty here with no
end date, and the only one no gate can enforce.

### Housekeeping already true
`THIRD_PARTY_NOTICES.txt` and `about.qt_source_desc` already state the source is hosted alongside
the binary. Left as-is deliberately: it describes the arrangement at conveyance, and softening it
now would add a second thing to remember to restore. The caveat stands — the existing
`build/package/SnapCap-0.1.0-win64.zip` carries that claim, so **it must not reach a tester before
the release page exists.**

---

## 5. H.26x patents

**Not a licence issue, and nothing in §4 touches it.** Copyright licensing (LGPL) and patent
licensing are separate systems; satisfying the LGPL perfectly says nothing about patents.

**The exposure.** `tools/build_ffmpeg.sh` enables `libopenh264` (H.264) and `libkvazaar` (H.265)
as software encoders, plus `h264_*`/`hevc_*` hardware encoders across NVENC/QSV/AMF/MediaFoundation.
H.264 is administered by MPEG-LA (now Via LA) and H.265 by Access Advance/others; both carry
patent-licensing regimes that **AV1 and VP9 do not**. Distributing an encoder is the act that
raises the question. The AOM Patent License 1.0 shipping with SVT-AV1 is the opposite case — an
explicit grant.

**The caveat that is easy to get backwards.** Cisco's royalty coverage for OpenH264 attaches to
**Cisco's own precompiled binary** — which is why Firefox downloads it at runtime instead of
bundling it. The source is BSD, but compiling it ourselves, as `build_ffmpeg.sh` does, **does not
inherit that coverage.** Anyone reasoning "OpenH264 is the royalty-free one" is reasoning about a
distribution arrangement we are not using.

**Decision (2026-08-04): ship H.264/H.265 knowingly.** MP4/H.264 is what users expect to be able
to hand to anyone; AV1/VP9 in WebM is not an equivalent product. This is a deliberate, informed
choice, not an oversight — informational, never a build gate.

**The documented fallback**, specified so it is a half-day rather than a research project if ever
needed. It is *not* "stop shipping FFmpeg" — that would be the wrong lever, since it costs
unzip-and-run without addressing patents any better:

1. In `tools/build_ffmpeg.sh`, drop `--enable-libopenh264` and `--enable-libkvazaar`.
2. From the `--enable-encoder=` lists, drop `libopenh264`, `libkvazaar`, and every `h264_*` /
   `hevc_*` hardware name. **Keep `av1_nvenc`, `av1_qsv`, `av1_amf`, `av1_mf`.**
3. Keep the H.264/H.265 **decoders** — playback and trim of existing files is unaffected, and
   decoding is not the act that raises the licensing question in the same way.
4. Update `THIRD_PARTY_NOTICES.txt` (two sub-library rows disappear), `RecordingSpec.h`'s codec
   list, and the Preferences → Recording codec UI.
5. **Re-run the §1.3 checks.** A stripped configure line fails at *runtime*, not build
   time — §1.3's traps are this failure, five times over.

**Revisit if any of these become true:** SnapCap is monetised; distribution reaches a scale where
licensors take notice; or a licensor makes contact. Any of them makes "ask a lawyer" proportionate,
which it currently is not.

---

## 6. Summary of what gates what

| Duty | Enforced by | Fails how |
|---|---|---|
| FFmpeg source published | `install(CODE)` URL check | `FATAL_ERROR` under `-DSNAPCAP_RELEASE=ON` |
| Qt source self-hosted | `install(CODE)` URL check | `FATAL_ERROR` under `-DSNAPCAP_RELEASE=ON` |
| DLLs dynamic + replaceable | portable-ZIP architecture | structurally impossible to break |
| Notices ship beside exe | `CMakeLists.txt` POST_BUILD copy + `install()` | silent — About buttons go dead (Step 7 catches it) |
| Source published **before** the ZIP | **nothing** | human ordering rule — §4 Step 6 |
| Source stays available | **nothing** | human retention rule — §4 Ongoing |
| Published source **matches the shipped DLLs** | **nothing** — the gate checks the URL's shape, not its contents | **current** as of 2026-10-20 (O5 cleared); re-opens on every FFmpeg rebuild or build-script edit |

The bottom three rows are the residual risk. All are human acts that no build system can check,
which is why they are stated twice in this file. The third has already bitten once — the 2026-08-04
rebuild invalidated the published bundle and nothing complained (O3, §2).
