<!-- page: group="Guide" order=3 sources=include/impossible/text.h,include/impossible/file.h,src/file.c,src/text.c reviewed=2026-11-24 -->
# Text and files

How do you read, build and save text? Paths and text are UTF-8 everywhere, a file is read whole, and a file you write is there complete or not at all.

## What does it look like together?

This program reads a file, says how long it is, and keeps a copy.

```c program=copy_file tier=0
#include <impossible/file.h>
#include <impossible/text.h>

int im_main(void) {
    int count;
    char **args = im_args(&count);
    if (!args || count != 3) {
        im_print("copy_file <from> <to>\n");
        return 2;
    }
    char *data;
    size_t length;
    if (im_read(args[1], 64 << 20, &data, &length) != IM_OK) return 1;

    im_text said = {0};
    im_add_number(&said, length, 1);
    im_add(&said, " bytes\n");
    bool told = !said.failed && im_print(said.data);
    im_text_free(&said);

    im_result kept = im_write(args[2], data, length, true, args[1]);
    im_free(data);
    im_args_free(args);
    return told && kept == IM_OK ? 0 : 1;
}
```

## Why does im_read ask for a limit?

So that a huge or endless file cannot take all the memory. It returns `IM_TOO_LARGE` and reads nothing more.

## How is a write safe?

`im_write` writes a temporary file beside the real one and renames it over. A program that is stopped in the middle leaves the old file whole. The last argument is part of the temporary name, so two writers at once need different ones.

> [!TIP]
> With `replace` false, only one of several writers makes the file; the others get `IM_EXISTS`. That is a simple lock between programs.

## What does each result mean?

| Result | Meaning |
| --- | --- |
| `IM_OK` | Done |
| `IM_EXISTS` | There is one already |
| `IM_MISSING` | The file or its folder is not there |
| `IM_TOO_LARGE` | More than the limit you gave |
| `IM_FAILED` | Windows refused, or there was no memory |

## How do texts grow?

Start an `im_text` as `{0}` and add to it. When memory runs out it stays as it was and `failed` is set, so you can add many times and look once at the end. [text.h](site:api-text.html) has the rest.
