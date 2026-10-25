#pragma once

#include "AudioEngine.h"
#include <stdexcept>

namespace xstnsions {

class ExtensionManager {
public:
    explicit ExtensionManager(std::filesystem::path library) : library_(std::move(library)) {
        std::filesystem::create_directories(library_);
    }

    const std::filesystem::path& libraryPath() const noexcept { return library_; }
    std::vector<ExtensionManifest> scan() const { return scanExtensions(library_); }

    std::string importFolder(const std::filesystem::path& folder) {
        if (!std::filesystem::is_directory(folder) || std::filesystem::is_symlink(folder))
            throw std::runtime_error("Choose an extension folder (symbolic links are not allowed)");
        const auto manifestPath = folder / "extension.json";
        if (!std::filesystem::is_regular_file(manifestPath))
            throw std::runtime_error("The selected folder must contain extension.json");
        const auto manifest = parseManifest(manifestPath);
        if (manifest.id.empty() || !std::all_of(manifest.id.begin(), manifest.id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        }) || manifest.id == "." || manifest.id == "..")
            throw std::runtime_error("Extension id may only contain letters, numbers, dots, dashes and underscores");
        const auto destination = library_ / manifest.id;
        if (std::filesystem::exists(destination)) throw std::runtime_error("An extension with this id is already installed");
        auto temporary = library_ / (".import-" + manifest.id);
        std::filesystem::remove_all(temporary);
        std::size_t totalBytes = 0;
        std::size_t fileCount = 0;
        try {
            for (std::filesystem::recursive_directory_iterator it(folder), end; it != end; ++it) {
                if (it->is_symlink()) throw std::runtime_error("Extension packages cannot contain symbolic links");
                if (it->is_directory()) continue;
                if (!it->is_regular_file()) throw std::runtime_error("Extension package contains a non-regular file");
                if (++fileCount > 20000) throw std::runtime_error("Extension contains too many files");
                const auto size = it->file_size();
                if (size > 512ULL * 1024ULL * 1024ULL - totalBytes)
                    throw std::runtime_error("Extension exceeds the 512 MiB local import limit");
                totalBytes += static_cast<std::size_t>(size);
                const auto relative = std::filesystem::relative(it->path(), folder);
                std::filesystem::create_directories((temporary / relative).parent_path());
                std::filesystem::copy_file(it->path(), temporary / relative,
                    std::filesystem::copy_options::none);
            }
            (void)parseManifest(temporary / "extension.json");
            std::filesystem::rename(temporary, destination);
        } catch (...) {
            std::filesystem::remove_all(temporary);
            throw;
        }
        return manifest.name;
    }

private:
    std::filesystem::path library_;
};

} // namespace xstnsions
