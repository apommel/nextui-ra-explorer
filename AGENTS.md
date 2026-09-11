# Notes for agents

Read `README.md` first for what the project is and how to build it. This file
records the things that are not visible from the code and that tend to cause
silent breakage.

## Working style

There is no test suite, and most of the code needs SDL and a device. Verify
pure logic by lifting the real function out of the source into a throwaway
program in a scratch directory and running it against real API responses —
sorting, formatting, field mapping and the settings round-trip have all been
checked this way. Do not paraphrase the function into the harness; copy it, or
compile the actual source file.

Both builds must stay warning-clean. Use a fresh build directory when checking:
an existing one may have been configured before an option changed.

Do not claim anything works on hardware. Nothing here has been verified beyond
compiling and, for the API layer, running against the live service.

## Device builds

There is one device build. Since Apostrophe v1.2.0 the platform is resolved at
runtime — `ap_get_platform()` reads the launcher's `PLATFORM`, falling back to
the basename of `SYSTEM_PATH` — so `-DPLATFORM_NEXTUI` is the only platform
define and the resulting binary runs on tg5040, tg5050, my355 and h700. Nothing
selects a device at compile time any more; do not reintroduce a `PLATFORM_*`
define or a per-platform cross file.

The build uses the tg5040 toolchain image **pinned by digest** in
`docker/Dockerfile`, the same digest Apostrophe pins for `make universal`. One
image now compiles the binary that ships to every handheld, so its ABI baseline
(`-mcpu=cortex-a53`, glibc, SDL2 2.26.1) has to stay fixed. Moving to a tag
would let a sysroot bump silently raise the floor under devices that cannot
meet it.

Consequences worth knowing:

- `cross/pkgconfig` comes **first** in `PKG_CONFIG_PATH`, ahead of the sysroot.
  `SDL2_ttf.pc` and `SDL2_image.pc` are there because the sysroot has neither.
  `sdl2.pc` is there to override one that exists: the sysroot's puts
  `-Wl,-rpath,/usr/lib` in `Libs`, and a binary that has to load whichever SDL2
  the firmware supplies must carry no runtime search path of its own. Check
  with `readelf -d` that the binary has no `RPATH` or `RUNPATH`.
- `-mcpu=cortex-a53` is correct even though tg5050 and my355 are A55. It is not
  just the ISA floor: GCC 8.3 maps `-mtune=cortex-a55` onto the A53 scheduler and
  cost model, so tuning for A55 emits byte-identical code. Nothing is lost, and
  `-mtune=generic` would only replace an exact model with a compromise.
- `--force-fallback-for=openssl` in the `device` task is load-bearing. Without
  it Meson silently prefers the sysroot OpenSSL — 1.1.0i, which is end-of-life
  — and links it dynamically, so the binary then depends on a library the
  device may not have.
- The device compiler is GCC 8.3 with `-Wextra -Werror`. It catches things
  Apple clang does not, `-Wformat-truncation` in particular. A native build
  passing proves little.

Other build facts: the toolchain image has Ninja but not Meson, which
`docker/Dockerfile` adds; `libm` must be linked explicitly because it is
separate from libc on Linux; and `AP_S()` cannot be used outside `main.c`
because it dereferences a global that only exists in the translation unit
defining `AP_IMPLEMENTATION` — call `ap_scale()` instead.

## Packaging

`task package` produces one `dist/RA.Explorer.pak.zip` whose **contents sit at
the archive root**, not under a `RA Explorer.pak/` folder and not under
`Tools/`. NextUI creates the directory itself, under whichever platform it is
running, which is what makes a single archive installable everywhere. Zipping
the folder instead would nest it one level too deep on install.

## Apostrophe constraints

Apostrophe is a vendored subproject (`subprojects/apostrophe.wrap`, pinned to a
tag). Prefer working within it; a local patch is a maintenance burden.

- **`ap_list` has no way to disable wrap-around.** Up at the first row jumps to
  the last, unconditionally.
- **Do not grow `item_count` while `ap_list` is running.** It re-reads
  `opts->items` and `item_count` every frame, which makes this look safe, but
  `item_alpha` is allocated once at the initial count — growing it overruns the
  heap.
- **`footer_update` runs every frame**, with the live cursor and a mutable
  `opts`. It is a general per-frame hook despite the name, and it is what
  drives lazy icon loading. Anything it does must be cheap and must not retry
  failures indefinitely.
- **`ap_detail_screen` always reports A.** It cannot be disabled, so screens
  that want A unbound `continue` on `AP_DETAIL_ACTION`. Re-entering resets the
  scroll position, since the widget takes no scroll parameter.
- **`ap_options_list` frees and replaces the option strings of
  `AP_OPT_KEYBOARD` rows** when the keyboard is confirmed. Those must be heap
  allocated. `AP_OPT_STANDARD` and `AP_OPT_CLICKABLE` rows are only indexed, so
  literals are fine there.
- **`ap_keyboard`: B is backspace, Y cancels, START confirms.** `help_text` is
  not an on-screen label — it replaces the built-in key help in the Menu
  overlay, so passing a prompt there hides the text explaining Y.
- **`ap_list` owns neither item strings nor item textures.** Callers free both.
- **`AP_SECTION_IMAGE` stretches to `image_w`/`image_h` with no aspect
  correction**, and defaults to 300x200. Square art needs both set equal.
- **The SDL renderer is single-threaded.** Texture creation must stay on the
  thread that created it, which rules out loading images on a worker.

## RetroAchievements API

- **A rejected key returns HTTP 401 with a well-formed JSON body.** Parsing
  alone cannot detect it; `client.c` checks the status code. This is also how
  `RA_GetLastError` distinguishes a bad key from an unreachable server.
- **`GetGameInfoAndUserProgress` needs `a=1`** for `HighestAwardKind` and
  `HighestAwardDate`. Without it they are absent even when earned.
- **Field naming is inconsistent.** Keys are mostly PascalCase, but achievement
  `Type` values are snake_case (`win_condition`) while award kinds are
  hyphenated (`beaten-hardcore`). Lookups are case-sensitive and fail silently,
  so a wrong case renders a dash rather than erroring.
- **`UserTotalPlaytime` is seconds**, not minutes.
- **Absence is meaningful.** Locked achievements omit `DateEarned`;
  `DateEarnedHardcore` appears only on a hardcore unlock; `Type` is null for
  most achievements; `NumDistinctPlayers` lives on the game, not the
  achievement.
- **There is no search endpoint.** `RA_SearchGames` uses
  `retroachievements.org/internal-api/search`, which is the website's own
  endpoint: undocumented, no compatibility promise, and it filters by
  User-Agent (a default Python one gets 403). Callers must degrade gracefully.
- **`GetUserRecentAchievements` has no count parameter**, so the result is
  truncated client-side to `RA_LIST_MAX`.
- **Image paths come in two shapes.** The public API returns paths like
  `/Images/067895.png`; the search endpoint returns absolute URLs.
  `RA_GetImage` accepts either.
- **No endpoint reports who the API key belongs to.** Every user-scoped call
  needs an explicit `u`, which accepts a username or a ULID interchangeably.
  Usernames are not stable — users can rename — so Settings resolves one to a
  ULID via `GetUserProfile` when credentials change, and `Settings_UserRef()`
  returns whichever is available. Resolution failing is never fatal.
- **`cJSON_GetNumberValue` returns NaN for a missing field**, and casting NaN
  to `int` is undefined behaviour. Use the `GetIntValue` helper.

## Conventions in this codebase

- In `Achievement`, `NULL` and `-1` mean "this source does not provide it", as
  distinct from empty or zero. Detail rows are omitted rather than showing a
  placeholder.
- Views take mapped structs, not raw cJSON, so they do not encode response
  shapes. The two achievement endpoints disagree enough to need a mapper each.
- Credentials live only in `settings.json`. Never commit a key or add one to a
  source file.
- `THIRD-PARTY-NOTICES.txt` is checked in and maintained by hand, and is copied
  into the Pak by `task pak`. Adding, removing or upgrading a bundled
  dependency means editing it — OpenSSL in particular is Apache-2.0, which
  requires its licence text to be distributed. SDL2 is excluded on purpose: it
  is linked dynamically and supplied by the device.
- Image downloads are non-blocking: `RA_ImageFetch*` wraps a curl multi handle
  pumped once per frame. Only the pre-fetch of the first screenful waits, and
  only because no frame is being drawn yet.
