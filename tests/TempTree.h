// Copyright (c) 2026 LG Electronics, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef TESTS_TEMPTREE_H_
#define TESTS_TEMPTREE_H_

#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <ftw.h>

// A throwaway directory tree, so tests that need real files on disk (the
// application scan, the locale path resolver) can build one and have it removed
// again whatever the outcome of the test.
class TempTree {
public:
    TempTree()
    {
        char pattern[] = "/tmp/sam_test_XXXXXX";
        const char* made = mkdtemp(pattern);
        m_root = (made != nullptr) ? made : "";
    }

    ~TempTree()
    {
        if (!m_root.empty())
            nftw(m_root.c_str(), unlinkEntry, 16, FTW_DEPTH | FTW_PHYS);
    }

    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;

    const std::string& root() const { return m_root; }

    std::string path(const std::string& relative) const { return m_root + "/" + relative; }

    // Create every directory along relative, like mkdir -p.
    std::string mkdirs(const std::string& relative) const
    {
        std::string accumulated = m_root;
        size_t start = 0;
        while (start <= relative.size()) {
            const size_t slash = relative.find('/', start);
            const std::string part = relative.substr(start, slash - start);
            if (!part.empty()) {
                accumulated += "/" + part;
                mkdir(accumulated.c_str(), 0755);
            }
            if (slash == std::string::npos)
                break;
            start = slash + 1;
        }
        return accumulated;
    }

    // Write contents to relative, creating any parent directories first.
    std::string write(const std::string& relative, const std::string& contents) const
    {
        const size_t slash = relative.find_last_of('/');
        if (slash != std::string::npos)
            mkdirs(relative.substr(0, slash));

        const std::string full = path(relative);
        std::ofstream out(full.c_str(), std::ios::trunc);
        out << contents;
        out.close();
        return full;
    }

private:
    static int unlinkEntry(const char* path, const struct stat*, int typeflag, struct FTW*)
    {
        if (typeflag == FTW_DP)
            rmdir(path);
        else
            unlink(path);
        return 0;
    }

    std::string m_root;
};

#endif  // TESTS_TEMPTREE_H_
