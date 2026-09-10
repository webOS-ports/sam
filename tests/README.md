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

Verified by mutation: reintroducing each original defect makes the
corresponding test fail.

## Not covered

The bus clients (`WAM`, `LSM`, `ApplicationManager`, ...) are singletons that
register on luna-service in their constructors, so they need a live bus or a
seam that does not exist yet. `SAMConf`'s locale accessors are likewise
untested: showing the stale-value behaviour needs `setLocale()`, which writes
its config out. Both are worth revisiting if these classes ever grow a
constructor that takes its dependencies.
