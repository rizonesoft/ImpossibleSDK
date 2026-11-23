<!-- page: group="Guide" order=6 sources=tools/contribute.py reviewed=2026-11-24 -->
# Adding to the SDK from a program

What if your program needs something a second program could use? Build it in the SDK, then move your program's pin.

## How do you make the change?

`contribute.py` gives you a folder of your own with the newest SDK, so that you never work in a checkout another session may be using. Run it from your program's folder.

```sh
python3 ../ImpossibleSDK/tools/contribute.py start
python3 ../ImpossibleSDK/tools/contribute.py finish <folder>
```

Make and commit the change in the folder that `start` prints. `finish` brings it onto the newest main, builds and tests that very commit, pushes it and pins your program to it.

## What does a change need?

A test that fails before it and passes after it. The rest is in [Contributing](site:contributing.html).
