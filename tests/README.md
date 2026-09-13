# sam unit tests

gtest-based tests for the parts of SAM that can be exercised without a running
luna-service bus: the JSON/​path/logging utilities and the application-description
scan.

Everything except `src/Main.cpp` builds into a `sam-core` static library, and
both the daemon and `sam_test` link it, so the tests run against the same
objects that ship.

## Building

Tests are off by default; an ordinary build does not compile them and does not
need gtest at build time:

    cmake <src> -DWEBOS_CONFIG_BUILD_TESTS=TRUE     # build only
    cmake <src> -DWEBOS_CONFIG_INSTALL_TESTS=TRUE   # build and install

`sam.bb` passes `WEBOS_CONFIG_INSTALL_TESTS`, so `bitbake sam` produces a
`sam-tests` package installing `sam_test` to `${webos_testsdir}/sam`.

gtest comes prebuilt from the sysroot through pkg-config. `webos_use_gtest()`
is deliberately not used: it expects gtest *sources* under
`${WEBOS_INSTALL_SRCDIR}/gtest`, which the Yocto `gtest` recipe does not stage.

## Running

On target:

    /usr/opt/webos/tests/sam/sam_test

Nothing here talks to the bus or touches system paths — `TempTree` gives each
test its own directory under `/tmp`, and the fixtures that reach SAMConf point
`$HOME` at it so its read-write config stays inside the sandbox.

A cross-built x86-64 binary also runs on an x86-64 build host, because the
Yocto sysroot ships its own loader:

    LD_LIBRARY_PATH=<sysroot>/usr/lib \
      <sysroot>/usr/lib/ld-linux-x86-64.so.2 <build>/tests/sam_test

The ARM builds run under qemu user-mode, which is worth doing rather than
trusting the x86-64 run alone: armv7 is 32-bit, so `ssize_t` and `gsize` are
half the width they are on the other targets.

    qemu-arm     -L <sysroot> -E LD_LIBRARY_PATH=<sysroot>/usr/lib <build>/tests/sam_test
    qemu-aarch64 -L <sysroot> -E LD_LIBRARY_PATH=<sysroot>/usr/lib <build>/tests/sam_test

The suite passes on x86-64, armv7 and aarch64 (the latter against the
halium-arm64 sysroot), and on a real device.

Running it on a device is worth doing rather than trusting emulation:
`JValueUtil::getSchema()` falls back to `JSchema::AllSchema()` when the schemas
are not installed, so a synthetic `appinfo.json` missing a field the real
`ApplicationDescription.schema` requires passes on a build host and fails on a
target. Push the binary and run it there:

    adb push tests/sam_test /tmp/sam_test
    adb shell 'chmod 0755 /tmp/sam_test && cd /tmp && HOME=/tmp/samtest-home ./sam_test'

`AppDescriptionScanTest.ScansASyntheticApplication` guards exactly that: if the
fixtures stop satisfying the schema it says so directly, instead of every
scan-based test failing on some later assertion.

## What is covered, and why

Each suite guards a defect that static analysis found in this tree, so a
regression shows up as a failing test rather than as a field report:

| Suite | Guards |
| --- | --- |
| `JValueUtilTest` | `getValue()` never writes its out-parameter on a failure path. Three uninitialized-read defects came from callers assuming it did. |
| `LoggerFormatTest` | `format()` is reentrant and does not truncate. It used to build results in a shared `static char[1024]`. |
| `FileTest` | `join`/`trimPath`/`concatToFilename` edge cases and the stat-based predicates. |
| `AnchorLocalePathTest` | Both localized-appinfo conventions, which wins when both resolve, and that the result is never absolute. |
| `AppDescriptionScanTest` | Version parsing against a third-party `appinfo.json`, including versions that used to throw out of the scan. |
| `AppDescriptionCompareTest` | Version ordering and the `appLocation` tiebreak that a self-comparison had disabled. |
| `SAMConfLocaleTest` | `setLocale()` round-trip, persistence, reload, and that a locale removed from the config reads back as `""` rather than as the previous value. |
| `LocalizedAppinfoTest` | Which `resources/<language>[/<script>][/<region>]` overlays a scan applies, and that a legacy relative `main` is re-anchored exactly once. |
| `SignalHandlerTest` | That signals arrive as main-loop events and not through a handler: no `sigaction` handler installed, the handled set blocked, `SIGPIPE` left at `SIG_IGN`, and a raised `SIGTERM` read back off the signalfd instead of killing the process. |

Verified by mutation: reintroducing each original defect makes the
corresponding test fail. For `SignalHandlerTest` that was done two ways -
reinstalling an `SA_SIGINFO` handler alongside the signalfd fails
`InstallsNoHandlerForTheSignalsItTakes`, and skipping the `sigprocmask()` fails
`BlocksThoseSignalsProcessWide` and then kills the test binary outright on the
next case, which is the point. One case does **not** hold, and is called out in the
test itself — removing the "skip empty locale components" change leaves every
test green, because `resources/en//` and `resources/en/` are the same directory
to POSIX and re-applying an overlay is idempotent. That change is tidiness
rather than behaviour, so there is nothing to regress.

## Not covered

The bus clients (`WAM`, `LSM`, `ApplicationManager`, ...) are singletons that
register on luna-service in their constructors, so they need a live bus or a
seam that does not exist yet. They are worth revisiting if these classes ever
grow a constructor that takes its dependencies.

Nothing here starts the daemon. `sam` registering on the bus, answering
`launch`, and driving an application through its lifecycle are all untested by
this suite and still need a device or a booted image.
 Treat a green run as
"the logic these tests reach is sound on this architecture", not as
"safe to ship".

## Two things to know before extending this

`SAMConfLocaleTest` restores `$HOME` and re-initializes `RuntimeInfo` on
teardown but deliberately does **not** re-initialize `SAMConf`.
`loadReadWriteConf()` creates `$HOME/.config/sam-conf.json` when it cannot parse
one, so re-reading it with the real `$HOME` back in place would write into the
developer's home directory.

The singletons live for the whole process, so a fixture that changes global
state has to put it back. `SAMConfLocaleTest` resets the locale to empty on
teardown; the `AppDescription` scan tests rely on that.
