#include <impossible/file.h>
#include <impossible/text.h>
#include <windows.h>

static im_result why(DWORD error) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND: return IM_MISSING;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS: return IM_EXISTS;
    default: return IM_FAILED;
    }
}

/* A file another program is just renaming or scanning is briefly refused. */
static bool passing(int attempt, DWORD error) {
    if (attempt >= 50 || (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED && error != ERROR_LOCK_VIOLATION)) return false;
    Sleep(4);
    return true;
}

static im_result take_all(HANDLE file, size_t most, char **data, size_t *length) {
    im_text text = {0};
    char part[4096];
    DWORD got = 0;
    for (;;) {
        if (!ReadFile(file, part, sizeof part, &got, NULL)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) break;
            im_text_free(&text);
            return IM_FAILED;
        }
        if (!got) break;
        if (got > most - text.length) {
            im_text_free(&text);
            return IM_TOO_LARGE;
        }
        im_add_bytes(&text, part, got);
        if (text.failed) break;
    }
    if (!text.data) im_add_bytes(&text, "", 0);
    if (text.failed) {
        im_text_free(&text);
        return IM_FAILED;
    }
    *data = text.data;
    *length = text.length;
    return IM_OK;
}

im_result im_read(const char *path, size_t most, char **data, size_t *length) {
    wchar_t *wide = im_wide(path);
    if (!wide) return IM_FAILED;
    HANDLE file;
    DWORD error = 0;
    for (int attempt = 0;; attempt++) {
        file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        if (file != INVALID_HANDLE_VALUE) break;
        error = GetLastError();
        if (!passing(attempt, error)) break;
    }
    im_result result = file == INVALID_HANDLE_VALUE ? why(error) : take_all(file, most, data, length);
    im_free(wide);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return result;
}

im_result im_read_input(size_t most, char **data, size_t *length) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    if (!in || in == INVALID_HANDLE_VALUE) return IM_FAILED;
    return take_all(in, most, data, length);
}

im_result im_write(const char *path, const void *data, size_t length, bool replace, const char *unique) {
    im_text temp = {0};
    im_add(&temp, path), im_add(&temp, "."), im_add(&temp, unique), im_add(&temp, ".part");
    wchar_t *from = temp.failed ? NULL : im_wide(temp.data), *to = im_wide(path);
    im_result result = IM_FAILED;
    im_text_free(&temp);
    if (!from || !to) goto done;
    HANDLE file = CreateFileW(from, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        result = GetLastError() == ERROR_PATH_NOT_FOUND ? IM_MISSING : IM_FAILED;
        goto done;
    }
    const char *at = data;
    bool good = true;
    for (size_t left = length; good && left;) {
        DWORD part = left > 0x100000 ? 0x100000 : (DWORD)left, wrote = 0;
        good = WriteFile(file, at, part, &wrote, NULL) && wrote;
        at += wrote, left -= wrote;
    }
    good = good && FlushFileBuffers(file);
    CloseHandle(file);
    if (good) {
        for (int attempt = 0;; attempt++) {
            if (MoveFileExW(from, to, MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0))) {
                result = IM_OK;
                break;
            }
            DWORD error = GetLastError();
            result = !replace && GetFileAttributesW(to) != INVALID_FILE_ATTRIBUTES ? IM_EXISTS : why(error);
            if (result == IM_EXISTS || !passing(attempt, error)) break;
        }
    }
    if (result != IM_OK) DeleteFileW(from);
done:
    im_free(from);
    im_free(to);
    return result;
}

im_result im_make_folder(const char *path) {
    wchar_t *wide = im_wide(path);
    if (!wide) return IM_FAILED;
    im_result result = CreateDirectoryW(wide, NULL) ? IM_OK : why(GetLastError());
    if (result == IM_EXISTS) {
        DWORD kind = GetFileAttributesW(wide);
        result = kind != INVALID_FILE_ATTRIBUTES && kind & FILE_ATTRIBUTE_DIRECTORY ? IM_OK : IM_FAILED;
    }
    im_free(wide);
    return result;
}

im_result im_move(const char *from, const char *to) {
    wchar_t *a = im_wide(from), *b = im_wide(to);
    im_result result = a && b && MoveFileExW(a, b, MOVEFILE_WRITE_THROUGH) ? IM_OK : a && b ? why(GetLastError()) : IM_FAILED;
    im_free(a);
    im_free(b);
    return result;
}

im_result im_delete(const char *path) {
    wchar_t *wide = im_wide(path);
    im_result result = !wide ? IM_FAILED : DeleteFileW(wide) ? IM_OK : why(GetLastError());
    im_free(wide);
    return result;
}

bool im_exists(const char *path) {
    wchar_t *wide = im_wide(path);
    bool there = wide && GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES;
    im_free(wide);
    return there;
}

im_result im_list(const char *path, bool (*each)(void *user, const char *name, bool folder), void *user) {
    im_text pattern = {0};
    im_add(&pattern, path), im_add(&pattern, "\\*");
    wchar_t *wide = pattern.failed ? NULL : im_wide(pattern.data);
    im_text_free(&pattern);
    if (!wide) return IM_FAILED;
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(wide, &found);
    im_free(wide);
    if (search == INVALID_HANDLE_VALUE) return why(GetLastError());
    im_result result = IM_OK;
    bool more = true;
    while (more) {
        const wchar_t *n = found.cFileName;
        if (!(n[0] == L'.' && (!n[1] || (n[1] == L'.' && !n[2])))) {
            char *name = im_utf8(n);
            if (!name) {
                result = IM_FAILED;
                break;
            }
            more = each(user, name, (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
            im_free(name);
            if (!more) break;
        }
        if (!FindNextFileW(search, &found)) {
            if (GetLastError() != ERROR_NO_MORE_FILES) result = IM_FAILED;
            break;
        }
    }
    FindClose(search);
    return result;
}

uint64_t im_now(void) {
    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    return (((uint64_t)time.dwHighDateTime << 32 | time.dwLowDateTime) - 116444736000000000ull) / 10;
}
