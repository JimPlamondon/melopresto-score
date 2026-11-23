/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clappluginsscanner.h"

#include <cstdlib>
#include <filesystem>
#include <set>

#include "../claptypes.h"

#include "log.h"

using namespace muse;
using namespace muse::clap;

namespace muse::clap {
static std::string clapEnvironmentValue(const char* name)
{
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

std::vector<std::string> clapStandardFolders()
{
    std::vector<std::string> folders;
    const std::string home = clapEnvironmentValue("HOME");
#if defined(__APPLE__)
    folders.push_back("/Library/Audio/Plug-Ins/CLAP");
    if (!home.empty()) {
        folders.push_back(home + "/Library/Audio/Plug-Ins/CLAP");
    }
#elif defined(_WIN32)
    const std::string common = clapEnvironmentValue("COMMONPROGRAMFILES");
    if (!common.empty()) {
        folders.push_back(common + "\\CLAP");
    }
    const std::string local = clapEnvironmentValue("LOCALAPPDATA");
    if (!local.empty()) {
        folders.push_back(local + "\\Programs\\Common\\CLAP");
    }
#else
    if (!home.empty()) {
        folders.push_back(home + "/.clap");
    }
    folders.push_back("/usr/lib/clap");
#endif
    return folders;
}

std::vector<std::string> clapFoldersOfPathValue(const std::string& clapPathValue)
{
#if defined(_WIN32)
    const char separator = ';';
#else
    const char separator = ':';
#endif
    std::vector<std::string> folders;
    size_t start = 0;
    while (start <= clapPathValue.size()) {
        size_t end = clapPathValue.find(separator, start);
        if (end == std::string::npos) {
            end = clapPathValue.size();
        }
        if (end > start) {
            folders.push_back(clapPathValue.substr(start, end - start));
        }
        start = end + 1;
    }
    return folders;
}

std::vector<std::string> clapFindPlugins(const std::vector<std::string>& folders)
{
    namespace fs = std::filesystem;
    const std::string extension = "." + std::string(CLAP_PACKAGE_EXTENSION);
    std::set<std::string> found;

    for (const std::string& folder : folders) {
        std::error_code error;
        if (!fs::is_directory(fs::path(folder), error)) {
            continue;
        }
        fs::recursive_directory_iterator it(fs::path(folder), fs::directory_options::skip_permission_denied, error);
        const fs::recursive_directory_iterator end;
        while (!error && it != end) {
            const fs::path& path = it->path();
            if (path.extension() == extension) {
                found.insert(path.string());
                // A bundle is one plug-in file: nothing inside it is looked at.
                if (it->is_directory(error)) {
                    it.disable_recursion_pending();
                }
            }
            it.increment(error);
        }
    }
    return { found.begin(), found.end() };
}
}

io::paths_t ClapPluginsScanner::scanPlugins() const
{
    TRACEFUNC;

    std::vector<std::string> folders = clapStandardFolders();
    for (const std::string& folder : clapFoldersOfPathValue(clapEnvironmentValue("CLAP_PATH"))) {
        folders.push_back(folder);
    }

    io::paths_t result;
    for (const std::string& path : clapFindPlugins(folders)) {
        result.push_back(io::path_t(path));
    }
    return result;
}
