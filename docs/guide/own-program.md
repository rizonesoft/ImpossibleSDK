<!-- page: group="Start here" order=3 sources=tools/new_app.py,tools/app_build.py,cmake/ImpossibleSDK.cmake reviewed=2026-11-25 -->
# A program of your own

Where does your program live? In its own folder and repository, pinned to one commit of the SDK.

## What is in it?

Two files are enough. `im_main` is where the program starts, and its result is the exit code.

```c project=Hello
#include <impossible/base.h>

int im_main(void) {
    return im_print("Hello from Impossible SDK\n") ? 0 : 1;
}
```

```cmake project=Hello
cmake_minimum_required(VERSION 4.0)
project(Hello LANGUAGES C)
include("${IMPOSSIBLE_SDK_DIR}/cmake/ImpossibleSDK.cmake")
impossible_add_program(Hello CONSOLE TIER 0 SOURCES src/main.c)
```

`impossible_add_program` takes `CONSOLE` or `WINDOW`, the highest [tier](site:tiers.html) the program needs, and its source files.

## How do you start one?

`new_app.py` makes the repository: the two files, an engine with its own tests, `sdk.json` with the commit of the SDK, and `app.json`.

```sh
python tools/new_app.py ../Hello --label Hello --tier 1 --window --license gpl-3.0.txt
python tools/app_build.py ../Hello
```

`--license` is the GPL-3.0 text to copy: programs of the Impossible OS project are GPL-3.0-only, and `new_app.py` writes that into the new repository. For a program of your own under another license, change the license and the README afterwards. Leave out `--window` for a console program. [Commands](site:commands.html) has every option.

## What is sdk.json?

The commit the program is built with. `app_build.py` uses exactly that one: when your SDK checkout has moved on or has local changes, it copies the pinned commit out of the history and builds with the copy. Change the commit in `sdk.json` to move to a newer SDK. `--any-sdk` builds with the checkout as it is, for working on both at once.

## What is app.json?

What the program says about itself: its name, label and version, its tier, the file types it opens, and what it needs from Impossible OS that imports cannot show.

```json
{
  "name": "Photos",
  "label": "Photos",
  "version": "0.0.0",
  "tier": 2,
  "opens": [],
  "needs": []
}
```

## What is different from an ordinary C program?

There is no C runtime: no `printf`, no `malloc`, no `main`. Use [base.h](site:api-base.html), [text.h](site:api-text.html) and [file.h](site:api-file.html). The compiler may still call `memcpy`, `memmove`, `memset` and `memcmp`; the kit has them, and builds every translation unit with function and data sections so `--gc-sections` drops unused SDK helpers.
