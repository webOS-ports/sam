// Copyright (c) 2019-2025 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "File.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib.h>

namespace {

// Splits filePath into the directory to open and the name inside it.
bool splitPath(const string& filePath, string& dir, string& name)
{
    const size_t slash = filePath.find_last_of('/');
    if (slash == string::npos) {
        dir = ".";
        name = filePath;
    } else {
        dir = (slash == 0) ? "/" : filePath.substr(0, slash);
        name = filePath.substr(slash + 1);
    }
    return !name.empty() && name != "." && name != "..";
}

bool writeAll(int fd, const string& buffer)
{
    const char* data = buffer.data();
    size_t left = buffer.size();

    while (left > 0) {
        const ssize_t written = ::write(fd, data, left);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        data += written;
        left -= static_cast<size_t>(written);
    }
    return true;
}

// close() that keeps the errno of an earlier failure.
void closeKeepingErrno(int fd)
{
    const int saved = errno;
    close(fd);
    errno = saved;
}

}  // namespace

void File::set_slash_to_base_path(string& path)
{
    if (!path.empty() && path[path.length() - 1] == '/')
        return;

    path.append("/");
}

string File::readFile(const string& file_name)
{
    ifstream file(file_name.c_str(), ifstream::in);
    string file_contents;

    if (file.is_open() && file.good()) {
        stringstream buf;
        buf << file.rdbuf();
        file_contents = buf.str();
    }

    return file_contents;
}

string File::tempPathFor(const string& filePath)
{
    string dir, name;
    if (!splitPath(filePath, dir, name))
        return "";
    return File::join(dir, "." + name + ".tmp");
}

bool File::writeFile(const string& filePath, const string& buffer)
{
    string dir, name;
    if (!splitPath(filePath, dir, name)) {
        errno = EINVAL;
        return false;
    }
    const string tempName = "." + name + ".tmp";

    // Only the last component is not followed: the directory itself may well be
    // reached through a symlink (/tmp -> /var/tmp on oe-core).
    const int dirFd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirFd < 0)
        return false;

    mode_t mode = 0644;
    bool exists = false;
    struct stat current;
    if (fstatat(dirFd, name.c_str(), &current, AT_SYMLINK_NOFOLLOW) == 0) {
        if (S_ISLNK(current.st_mode)) {
            closeKeepingErrno(dirFd);
            errno = ELOOP;
            return false;
        }
        if (!S_ISREG(current.st_mode)) {
            closeKeepingErrno(dirFd);
            errno = EINVAL;
            return false;
        }
        mode = current.st_mode & 0755;
        exists = true;
    } else if (errno != ENOENT) {
        closeKeepingErrno(dirFd);
        return false;
    }

    // A leftover from a crash, or something planted there: unlinkat() removes a
    // symlink itself, never what it points to. If the name is taken again before
    // the open, O_EXCL | O_NOFOLLOW fails rather than write through it.
    if (unlinkat(dirFd, tempName.c_str(), 0) != 0 && errno != ENOENT) {
        closeKeepingErrno(dirFd);
        return false;
    }
    const int fd = openat(dirFd, tempName.c_str(),
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
    if (fd < 0) {
        closeKeepingErrno(dirFd);
        return false;
    }

    // fchmod() so that an existing file's mode is kept as it was, not less the umask.
    bool ok = (!exists || fchmod(fd, mode) == 0) && writeAll(fd, buffer) && fsync(fd) == 0;
    if (close(fd) != 0)
        ok = false;
    // rename() replaces whatever is at name, a symlink included, and never
    // follows it.
    if (ok && renameat(dirFd, tempName.c_str(), dirFd, name.c_str()) != 0)
        ok = false;
    if (!ok) {
        const int saved = errno;
        unlinkat(dirFd, tempName.c_str(), 0);
        close(dirFd);
        errno = saved;
        return false;
    }
    // Make the rename itself durable. Failing here does not undo it.
    (void) fsync(dirFd);
    close(dirFd);
    return true;
}

bool File::readStateFile(const string& path, string& contents)
{
    contents.clear();

    // O_NONBLOCK so that a FIFO in its place cannot stall SAM in open().
    const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return false;

    struct stat info;
    if (fstat(fd, &info) != 0) {
        closeKeepingErrno(fd);
        return false;
    }
    if (!S_ISREG(info.st_mode)) {
        close(fd);
        errno = EINVAL;
        return false;
    }
    if (info.st_uid != geteuid() || (info.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        close(fd);
        errno = EPERM;
        return false;
    }

    char chunk[4096];
    for (;;) {
        const ssize_t got = ::read(fd, chunk, sizeof(chunk));
        if (got < 0) {
            if (errno == EINTR)
                continue;
            closeKeepingErrno(fd);
            contents.clear();
            return false;
        }
        if (got == 0)
            break;
        contents.append(chunk, static_cast<size_t>(got));
    }
    close(fd);
    return true;
}

bool File::concatToFilename(const string& originPath, string& returnPath, const string& addingStr)
{
    if (originPath.empty() || addingStr.empty())
        return false;

    returnPath = "";

    string dir_path, filename, name_only, ext;
    size_t pos_dir = originPath.find_last_of('/');

    if (string::npos == pos_dir) {
        filename = originPath;
    } else {
        pos_dir = pos_dir + 1;
        dir_path = originPath.substr(0, pos_dir);
        filename = originPath.substr(pos_dir);
    }

    const size_t pos_ext = filename.find_last_of('.');

    if (string::npos == pos_ext)
        return false;

    name_only = filename.substr(0, pos_ext);
    ext = filename.substr(pos_ext);

    if (ext.length() < 2)
        return false;

    returnPath = dir_path + name_only + addingStr + ext;

    return true;
}

bool File::isDirectory(const string& path)
{
    struct stat dirStat;
    if (stat(path.c_str(), &dirStat) != 0) {
        return false;
    }
    if ((dirStat.st_mode & S_IFDIR) == 0) {
        return false;
    }
    return true;
}

bool File::isFile(const string& path)
{
    struct stat fileStat;

    if (stat(path.c_str(), &fileStat) != 0 || (fileStat.st_mode & S_IFREG) == 0) {
        return false;
    }
    return true;
}

bool File::makeDirectory(const string& path)
{
    if (g_mkdir_with_parents(path.c_str(), 0700) == 0)
        return true;
    return false;
}

bool File::createFile(const string& path)
{
    return File::writeFile(path, "");
}

bool File::deleteFile(const string& path)
{
    if(unlink(path.c_str()) == -1) {
        return false;
    }
    return true;
}

string File::join(const string& a, const string& b)
{
    string path = "";

    if (!a.empty() && a.back() == '/') {
        if (!b.empty() && b.front() == '/') {
            path = a + b.substr(1);
        }
        else {
            path = a + b;
        }
    } else {
        if (!b.empty() && b.front() == '/') {
            path = a + b;
        }
        else {
            path = a + "/" + b;
        }
    }
    return path;
}

File::File()
{
}

File::~File()
{
}
