/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../claptypes.h"

namespace muse::clap {
//! What one descriptor of a bundle's factory states.
struct ClapDescriptorInfo {
    std::string id;
    std::string name;
    std::string vendor;
    std::string version;
    std::vector<std::string> features;

    bool hasFeature(const std::string& feature) const;
};

//! One loaded CLAP file: its entry point, initialised, and its plug-in
//! factory. The entry is deinitialised and the library released when the
//! last owner lets go. Main thread only.
class ClapBundle
{
public:
    ~ClapBundle();
    ClapBundle(const ClapBundle&) = delete;
    ClapBundle& operator=(const ClapBundle&) = delete;

    //! Returns nullptr with the reason in \p problem.
    static std::shared_ptr<ClapBundle> open(const std::string& path, std::string& problem);

    const std::string& path() const;

    //! Every descriptor of the factory, in the factory's order. A plug-in is
    //! identified by its descriptor id, never by its place in this list.
    std::vector<ClapDescriptorInfo> descriptors() const;

    //! Creates the plug-in with that descriptor id; nullptr if there is none.
    const clap_plugin_t* createPlugin(const clap_host_t* host, const std::string& descriptorId) const;

private:
    ClapBundle() = default;

    std::string m_path;
    void* m_library = nullptr;
    const clap_plugin_entry_t* m_entry = nullptr;
    const clap_plugin_factory_t* m_factory = nullptr;
    bool m_initialised = false;
};

using ClapBundlePtr = std::shared_ptr<ClapBundle>;

//! The file inside the bundle that holds the code. On macOS the bundle's
//! Info.plist names it (CFBundle, Core Foundation Bundle); its name need not
//! match the bundle's. Elsewhere a ".clap" file is the library itself.
std::string clapExecutablePath(const std::string& bundlePath, std::string& problem);
}
