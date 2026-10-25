#include "AudioEngine.h"
#include "ExtensionManager.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::filesystem::path makeTemp() {
    const auto nonce = std::to_string(std::random_device{}());
    const auto path = std::filesystem::temp_directory_path() / ("xstnsions-test-" + nonce);
    std::filesystem::create_directories(path);
    return path;
}
void writeManifest(const std::filesystem::path& path, const std::string& id, const std::string& preset = "") {
    std::ofstream manifest(path / "extension.json");
    manifest << "{\"schemaVersion\":1,\"id\":\"" << id
             << "\",\"name\":\"Test\",\"version\":\"1.0\",\"kind\":\"preset\"";
    if (!preset.empty()) manifest << ",\"preset\":" << preset;
    manifest << '}';
}
void testBuiltInExtensionsAndPresetValues() {
#ifdef XSTNSIONS_SOURCE_DIR
    const auto root = std::filesystem::path(XSTNSIONS_SOURCE_DIR);
#else
    const auto root = std::filesystem::current_path();
#endif
    const auto aurora = xstnsions::parseManifest(root / "extensions/aurora-keys/extension.json");
    const auto velvet = xstnsions::parseManifest(root / "extensions/velvet-pulse/extension.json");
    require(aurora.id == "aurora-keys" && aurora.waveform == "sine" && aurora.delay.has_value(),
            "Aurora Keys manifest must expose a usable built-in sound preset");
    require(velvet.id == "velvet-pulse" && velvet.waveform == "pulse" && velvet.chorus.has_value(),
            "Velvet Pulse manifest must expose a usable alternate synthesizer preset");
}
void testInstrumentProducesAudio() {
    xstnsions::AudioEngine engine;
    engine.prepare(48000, 256);
    engine.noteOn(69, 1.0f);
    std::vector<float> left(256), right(256);
    float* channels[] = {left.data(), right.data()};
    engine.process(channels, 2, left.size());
    float peak = 0.0f;
    for (float sample : left) {
        require(std::isfinite(sample), "Instrument must produce finite samples");
        peak = std::max(peak, std::abs(sample));
    }
    require(peak > 0.001f, "A MIDI note must produce an audible signal");
}
void testMIDIAndEffects() {
    xstnsions::AudioEngine engine;
    engine.prepare(44100, 512);
    engine.setParameter("waveform", 1.0f);
    engine.setParameter("delay", 0.0f);
    engine.setParameter("chorus", 0.0f);
    engine.noteOn(60, 0.5f);
    std::vector<float> left(512), right(512);
    float* channels[] = {left.data(), right.data()};
    engine.process(channels, 2, 512);
    const float subduedPeak = *std::max_element(left.begin(), left.end());
    require(subduedPeak > 0.0f, "Sawtooth oscillator must produce audio");
    engine.noteOff(60);
    for (int block = 0; block < 28; ++block) {
        engine.process(channels, 2, 512);
        for (float sample : left) require(std::isfinite(sample), "Effect chain must not produce NaNs");
    }
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    engine.noteOn(48, 0.85f);
    engine.process(channels, 2, 512);
    require(*std::max_element(left.begin(), left.end()) > 0.0f,
            "A second note after release must retrigger the synth");
}
void testExtensionInstallAndValidation() {
    const auto temp = makeTemp();
    try {
        const auto package = temp / "Solar-Keys";
        std::filesystem::create_directories(package / "Presets");
        writeManifest(package, "solar-keys", "{\"waveform\":\"sine\",\"volume\":0.7}");
        { std::ofstream preset(package / "Presets" / "warm.json"); preset << "{}"; }
        xstnsions::ExtensionManager manager(temp / "Library");
        require(manager.importFolder(package) == "Test", "Importer should report the installed package name");
        const auto installed = manager.scan();
        require(installed.size() == 1 && installed[0].id == "solar-keys" && installed[0].volume == 0.7f,
                "Installed extension and preset values should be discoverable");
        bool duplicateRejected = false;
        try { manager.importFolder(package); } catch (const std::runtime_error&) { duplicateRejected = true; }
        require(duplicateRejected, "Duplicate extension ids must be rejected");
        const auto unsafe = temp / "unsafe";
        std::filesystem::create_directories(unsafe);
        writeManifest(unsafe, "../escape");
        bool pathRejected = false;
        try { manager.importFolder(unsafe); } catch (const std::runtime_error&) { pathRejected = true; }
        require(pathRejected, "Path traversal extension IDs must be rejected");
        const auto invalid = temp / "invalid";
        std::filesystem::create_directories(invalid);
        writeManifest(invalid, "invalid", "{\"volume\":99}");
        bool rangeRejected = false;
        try { manager.importFolder(invalid); } catch (const std::runtime_error&) { rangeRejected = true; }
        require(rangeRejected, "Out-of-range preset values must be rejected");
        const auto future = temp / "future";
        std::filesystem::create_directories(future);
        { std::ofstream manifest(future / "extension.json"); manifest << "{\"schemaVersion\":2}"; }
        bool schemaRejected = false;
        try { manager.importFolder(future); } catch (const std::runtime_error&) { schemaRejected = true; }
        require(schemaRejected, "Unknown extension schema versions must be rejected");
        const auto missing = temp / "missing";
        std::filesystem::create_directories(missing);
        { std::ofstream manifest(missing / "extension.json"); manifest << "{\"id\":\"missing\",\"name\":\"Missing\"}"; }
        bool missingSchemaRejected = false;
        try { manager.importFolder(missing); } catch (const std::runtime_error&) { missingSchemaRejected = true; }
        require(missingSchemaRejected, "Manifest schemaVersion must be explicitly provided");
        const auto linked = temp / "linked";
        std::filesystem::create_directories(linked);
        writeManifest(linked, "linked");
        std::filesystem::create_symlink(package, linked / "payload");
        bool symlinkRejected = false;
        try { manager.importFolder(linked); } catch (const std::runtime_error&) { symlinkRejected = true; }
        require(symlinkRejected, "Symlink payloads must be rejected");
    } catch (...) {
        std::filesystem::remove_all(temp);
        throw;
    }
    std::filesystem::remove_all(temp);
}
}
int main() {
    try {
        testBuiltInExtensionsAndPresetValues();
        testInstrumentProducesAudio();
        testMIDIAndEffects();
        testExtensionInstallAndValidation();
        std::cout << "PASS built-in presets and extension schema v1\nPASS audible synth signal generation\nPASS MIDI note release and effect processing\nPASS safe local extension import, ranges, duplicates and symlinks\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
