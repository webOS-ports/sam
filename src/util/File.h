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

#ifndef UTIL_FILE_H_
#define UTIL_FILE_H_

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>

using namespace std;

class File {
public:
    static void set_slash_to_base_path(string& path);
    static string readFile(const string& file_name);

    // Replace filePath with buffer, for the files SAM keeps its own state in.
    // SAM runs as root, so this never follows a symlink: the new contents go to
    // a temporary file next to filePath (opened O_EXCL | O_NOFOLLOW), are
    // fsync()ed, and renamed over filePath, so a reader or a crash sees either
    // the old file or the new one, never a truncated one. A file that is already
    // there keeps its permission bits, without group or other write; a new one
    // is created 0644 (less the umask).
    //
    // Fails, with errno set, if filePath is a symlink (ELOOP), is not a regular
    // file (EINVAL), or cannot be written; filePath is then left untouched.
    // Assumes one writer per path: a leftover temporary file from an earlier
    // crash is removed.
    static bool writeFile(const string& filePath, const string& buffer);

    // Read a file SAM itself keeps state in. Fails, with errno set, if path is a
    // symlink (ELOOP), not a regular file (EINVAL), or not owned by SAM's
    // effective uid or writable by group or others (EPERM): such a file was not
    // written by writeFile() and its contents are not trusted.
    static bool readStateFile(const string& path, string& contents);

    // The temporary file writeFile() uses for filePath (".<name>.tmp").
    static string tempPathFor(const string& filePath);
    static bool concatToFilename(const string& originPath, string& returnPath, const string& addingStr);

    static bool isDirectory(const string& path);
    static bool isFile(const string& path);

    // mkdir -p for a directory SAM keeps state in. Fails, with errno set, if path
    // itself is a symlink (ELOOP) or not a directory (ENOTDIR). As in writeFile(),
    // the parents may be reached through a symlink.
    static bool makeDirectory(const string& path);

    static bool createFile(const string& path);
    static bool deleteFile(const string& path);

    static string join(const string& a, const string& b);

    static void trimPath(string &path)
    {
        if (!path.empty() && path.back() == '/')
            path.erase(prev(path.end()));
    }

    File();
    virtual ~File();

};

#endif /* UTIL_FILE_H_ */
