/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clapbundle.h"

#include <algorithm>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using namespace muse::clap;

namespace muse::clap {
static std::string clapText(const char* text)
{
    return text ? std::string(text) : std::string();
}

//! The three loaders. macOS is the one that has been run; the Windows and
//! Linux ones are written to the CLAP specification and have not been run.
#if defined(__APPLE__)
std::string clapExecutablePath(const std::string& bundlePath, std::string& problem)
{
    CFURLRef bundleUrl = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
                                                                 reinterpret_cast<const UInt8*>(bundlePath.c_str()),
                                                                 static_cast<CFIndex>(bundlePath.size()), true);
    if (!bundleUrl) {
        problem = "not a path: " + bundlePath;
        return std::string();
    }
    CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, bundleUrl);
    CFRelease(bundleUrl);
    if (!bundle) {
        problem = "not a bundle: " + bundlePath;
        return std::string();
    }
    CFURLRef executableUrl = CFBundleCopyExecutableURL(bundle);
    CFRelease(bundle);
    if (!executableUrl) {
        problem = "the bundle names no executable: " + bundlePath;
        return std::string();
    }
    char buffer[4096] = {};
    const Boolean ok = CFURLGetFileSystemRepresentation(executableUrl, true, reinterpret_cast<UInt8*>(buffer), sizeof(buffer));
    CFRelease(executableUrl);
    if (!ok) {
        problem = "the bundle's executable has no file path: " + bundlePath;
        return std::string();
    }
    return std::string(buffer);
}

static void* clapOpenLibrary(const std::string& path, std::string& problem)
{
    void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        problem = clapText(dlerror());
    }
    return library;
}

static void* clapLibrarySymbol(void* library, const char* name)
{
    return dlsym(library, name);
}

static void clapCloseLibrary(void* library)
{
    dlclose(library);
}

#elif defined(_WIN32)
std::string clapExecutablePath(const std::string& bundlePath, std::string&)
{
    return bundlePath;
}

static void* clapOpenLibrary(const std::string& path, std::string& problem)
{
    const int length = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(length > 0 ? length : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), length);
    HMODULE library = LoadLibraryW(wide.c_str());
    if (!library) {
        problem = "LoadLibrary failed with code " + std::to_string(GetLastError());
    }
    return library;
}

static void* clapLibrarySymbol(void* library, const char* name)
{
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
}

static void clapCloseLibrary(void* library)
{
    FreeLibrary(static_cast<HMODULE>(library));
}

#else
std::string clapExecutablePath(const std::string& bundlePath, std::string&)
{
    return bundlePath;
}

static void* clapOpenLibrary(const std::string& path, std::string& problem)
{
    void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        problem = clapText(dlerror());
    }
    return library;
}

static void* clapLibrarySymbol(void* library, const char* name)
{
    return dlsym(library, name);
}

static void clapCloseLibrary(void* library)
{
    dlclose(library);
}

#endif
}

bool ClapDescriptorInfo::hasFeature(const std::string& feature) const
{
    return std::find(features.begin(), features.end(), feature) != features.end();
}

ClapBundle::~ClapBundle()
{
    if (m_initialised && m_entry) {
        m_entry->deinit();
    }
    if (m_library) {
        clapCloseLibrary(m_library);
    }
}

std::shared_ptr<ClapBundle> ClapBundle::open(const std::string& path, std::string& problem)
{
    const std::string executable = clapExecutablePath(path, problem);
    if (executable.empty()) {
        if (problem.empty()) {
            problem = "no executable in " + path;
        }
        return nullptr;
    }

    std::shared_ptr<ClapBundle> bundle(new ClapBundle());
    bundle->m_path = path;
    bundle->m_library = clapOpenLibrary(executable, problem);
    if (!bundle->m_library) {
        return nullptr;
    }

    bundle->m_entry = static_cast<const clap_plugin_entry_t*>(clapLibrarySymbol(bundle->m_library, "clap_entry"));
    if (!bundle->m_entry || !clap_version_is_compatible(bundle->m_entry->clap_version)
        || !bundle->m_entry->init || !bundle->m_entry->deinit || !bundle->m_entry->get_factory) {
        problem = "no usable clap_entry in " + executable;
        bundle->m_entry = nullptr;
        return nullptr;
    }

    // The entry is given the path of the bundle, not of the file inside it.
    if (!bundle->m_entry->init(path.c_str())) {
        problem = "clap_entry.init refused " + path;
        return nullptr;
    }
    bundle->m_initialised = true;

    bundle->m_factory = static_cast<const clap_plugin_factory_t*>(bundle->m_entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!bundle->m_factory || !bundle->m_factory->get_plugin_count || !bundle->m_factory->get_plugin_descriptor
        || !bundle->m_factory->create_plugin) {
        problem = "no plug-in factory in " + path;
        return nullptr;
    }

    return bundle;
}

const std::string& ClapBundle::path() const
{
    return m_path;
}

std::vector<ClapDescriptorInfo> ClapBundle::descriptors() const
{
    std::vector<ClapDescriptorInfo> result;
    const uint32_t count = m_factory->get_plugin_count(m_factory);
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const clap_plugin_descriptor_t* descriptor = m_factory->get_plugin_descriptor(m_factory, i);
        if (!descriptor || !descriptor->id || !*descriptor->id) {
            continue;
        }
        ClapDescriptorInfo info;
        info.id = descriptor->id;
        info.name = clapText(descriptor->name);
        info.vendor = clapText(descriptor->vendor);
        info.version = clapText(descriptor->version);
        for (const char* const* feature = descriptor->features; feature && *feature; ++feature) {
            info.features.emplace_back(*feature);
        }
        result.push_back(std::move(info));
    }
    return result;
}

const clap_plugin_t* ClapBundle::createPlugin(const clap_host_t* host, const std::string& descriptorId) const
{
    return m_factory->create_plugin(m_factory, host, descriptorId.c_str());
}
