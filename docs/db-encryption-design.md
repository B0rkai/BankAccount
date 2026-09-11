# Design: Real db encryption + Linux `.baf` support (proposed epic)

Status: **proposed, not implemented**. Discussed 2026-09-11, growing out of investigating
vendoring ZipLib so the Linux daemon ([linux-query-daemon-design.md](linux-query-daemon-design.md),
now fully implemented) could open `db\BData.baf` directly instead of requiring a plain-text
`BankAccount.txt` export.

## Goal

Two related but separable changes:

1. Let the Linux daemon (and any other headless reader) open `.baf` directly.
2. Replace the current password ("pass", hardcoded, "obfuscation not security" per CLAUDE.md) with
   a real user-chosen password, asked interactively when the db is loaded, that actually protects
   the data at rest.

(2) is the reason this doc exists rather than just extending story-by-story: (1) alone is a
straightforward vendoring/porting job, but a "real" password only means something if the cipher
underneath is real too — see "Why the current encryption doesn't count" below. Both are written up
together because they touch the same load path and because doing (1) first, with the current
cipher, would mean redoing it once (2) lands.

## Why the current encryption doesn't count

`BankAccountFile.cpp`'s `ZipSave()`/`Load()` use ZipLib's `SetPassword()`/`IsPasswordProtected()`,
which is the classic **"traditional" PKZip encryption** — confirmed by inspecting the vendored
ZipLib source (`C:\Users\<user>\source\ziplib`, zlib-licensed, Petr Beneš): no AES anywhere in the
tree, `streams/zip_cryptostream.h` implements only the traditional stream cipher.

That cipher is broken regardless of password strength or secrecy: it's vulnerable to a
known-plaintext attack (Biham–Kocher) that recovers the internal key state in seconds given ~12
bytes of known plaintext — and our own format guarantees an attacker that, since the single
`save.data` entry's content (`AccountManager::StreamOut`'s format) always starts with the same
predictable header bytes. Swapping the hardcoded `"pass"` for a real user password would still be
crackable near-instantly by any of the many existing zip-crack tools. **Real security requires a
different cipher, not just a different password.**

## Proposed approach

Decouple compression from confidentiality instead of asking ZipLib's zip container to do both:

- Keep Deflate compression (via the same trimmed ZipLib vendoring story 1 below describes) for the
  plain-text `save.data` payload, since transaction/category text compresses well.
- Encrypt the *compressed* bytes with a real authenticated cipher, keyed from the user's password
  via a deliberately slow key-derivation function (a human password is far lower-entropy than a
  real key — hashing it once into an AES/ChaCha key makes brute-forcing the password itself cheap;
  a KDF like Argon2 makes each guess expensive). Encrypted output doesn't compress, so this only
  works in that order (compress, then encrypt) — reversed, ZipLib would find nothing left to
  shrink.

### Library choice: vendor Monocypher

Researched rather than assumed, per this repo's own "actually evaluate a new dependency" norm:
[Monocypher](https://github.com/LoupVaillant/Monocypher) is a single `monocypher.c`/`monocypher.h`
pair, dual CC0-1.0/BSD-2-Clause licensed, zero dependencies, compiles as C99 or C++, actively
maintained. It provides both primitives needed here in one vendored file:

- **Argon2** (password → symmetric key).
- **XChaCha20-Poly1305** (authenticated encryption — detects tampering/corruption instead of
  silently decrypting garbage, unlike ZipLib's cipher).

This matches the exact vendoring precedent already established for `nlohmann/json.hpp` and
Chart.js/Grid.js (see [build-setup.md](build-setup.md)): single file(s), no build step, no
per-machine CMake/sibling-checkout setup — a much better fit than pulling in OpenSSL/mbedTLS for
two primitives, or hand-rolling AES-GCM.

## Story 1: vendor a trimmed ZipLib for compression, cross-platform

Full findings from the initial investigation (file counts, portability check) are in this
session's transcript; summary:

- `BankAccountFile.cpp` only ever uses `ZipFile::Open/SaveAndClose/ExtractEncryptedFile`,
  `CreateEntry/GetEntry`, `SetCompressionStream`/`GetDecompressionStream` — and always with the
  default `DeflateMethod`, never bzip2/LZMA.
- The full vendored ZipLib tree is ~2.2MB/~220 files, almost all of it `extlibs/lzma` (133
  files/1.1MB) and `extlibs/bzip2` (10 files/193K) this app never exercises. The wrapper itself is
  62 files/294K (only 7 `.cpp` to compile, rest header-only templates) + zlib's 22 files/485K for
  the Deflate codec — ~84 files/~780K needed.
- `methods/ZipMethodResolver.h` unconditionally `#include`s `Bzip2Method.h`/`LzmaMethod.h`, so
  trimming those out means a small patch to that one file — same "clone as our own fork, carry
  local commits" pattern as [wxCharts](wxcharts-patches.md), rather than verbatim vendoring like
  `nlohmann/json.hpp`.
- No `_WIN32`/`windows.h` anywhere in the wrapper or in zlib's actual codec files (only in zlib's
  own platform-guard headers) — confirmed portable to Linux/g++ as-is.

Work: fork ZipLib (mirroring the wxCharts fork precedent), trim to Store+Deflate, vendor the
resulting ~84 files directly under `include`/`src` (or a `third_party/` subfolder) compiled inline
by both `BankAccountCore.vcxproj` and the Makefile — no separate `.lib`, no sibling checkout, which
also fixes today's Debug-config pain point (currently depends on a locally-built
`..\..\ziplib\Bin\x64\Debug\*.lib` sibling). Extract the `.baf`-decode step out of
`BankAccountFile.cpp` (which stays Windows/journal-specific) into a small platform-agnostic helper
in `BankAccountCore`, called from both `BankAccountFile::Load()` and a new
`DaemonDb`-side loader — same shared-core pattern as `IJournal`.

## Story 2: vendor Monocypher, real password-based encryption

- Vendor `monocypher.c`/`monocypher.h` (see library choice above).
- On save: derive a key from the user's password with Argon2 (fixed or persisted salt — needs a
  decision: a random per-file salt stored alongside the ciphertext is standard practice and doesn't
  weaken security, so prefer that over a fixed salt), encrypt the Deflate-compressed payload with
  XChaCha20-Poly1305, store nonce + auth tag + ciphertext as the zip entry's content.
- On load: prompt for the password (new dialog — there is no prompt today, it's silent), derive the
  same key, decrypt-and-verify (Poly1305 tag rejects a wrong password or corrupted file cleanly,
  rather than ZipLib's silent garbage-decrypt).
- **No recovery by design**: forget the password, lose the db. Worth confirming this tradeoff is
  wanted before implementing — it's a real behavior change from today's frictionless load.

## Story 3: Linux daemon password handling

The daemon is unattended (systemd service, no interactive prompt) — it needs the password supplied
non-interactively. Proposed: same pattern as the existing `--token` (env-file-based, see
[linux-daemon-deployment.md](linux-daemon-deployment.md)), i.e. the password sits in the
0600-permissioned env file rather than being typed each time. Worth being explicit in the eventual
writeup that this means the daemon's security model stays "protected by file permissions on that
one host," not "asked at load" — a real, deliberate difference from the desktop's model, not an
oversight.

## Open question, not yet addressed

`db\journal.txt` (crash-recovery journal) is written in plain text regardless of db encryption —
real confidentiality for the db itself leaves that as a smaller but separate leak. Whether to
address it is a decision for whoever picks this up, not resolved here.

## Effort estimate

Not yet broken down into sized stories — this doc captures the investigation and the shape of the
plan; sizing happens when it's actually picked up.
