#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace xstnsions {

constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kMaxVoices = 32;
inline float clamp01(float value) { return std::max(0.0f, std::min(1.0f, value)); }
inline float midiToFrequency(int note) { return 440.0f * std::pow(2.0f, (note - 69) / 12.0f); }

struct JsonValue {
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    std::variant<std::nullptr_t, bool, double, std::string, Object, Array> value;
    const JsonValue* find(const std::string& key) const {
        const auto* object = std::get_if<Object>(&value);
        if (!object) return nullptr;
        const auto found = object->find(key);
        return found == object->end() ? nullptr : &found->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& source) : source_(source) {}
    JsonValue parse() {
        auto result = parseValue();
        whitespace();
        if (position_ != source_.size()) fail("Unexpected trailing data");
        return result;
    }
private:
    const std::string& source_;
    std::size_t position_ = 0;
    std::size_t depth_ = 0;
    std::size_t nodes_ = 0;
    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error(std::string("Invalid extension JSON: ") + message);
    }
    void whitespace() {
        while (position_ < source_.size() &&
               (source_[position_] == ' ' || source_[position_] == '\t' ||
                source_[position_] == '\r' || source_[position_] == '\n')) ++position_;
    }
    char consume() {
        if (position_ >= source_.size()) fail("unexpected end of file");
        return source_[position_++];
    }
    bool consumeIf(char value) {
        whitespace();
        if (position_ < source_.size() && source_[position_] == value) {
            ++position_;
            return true;
        }
        return false;
    }
    std::string parseString() {
        whitespace();
        if (consume() != '"') fail("expected string");
        std::string result;
        while (position_ < source_.size()) {
            const char c = consume();
            if (c == '"') return result;
            if (static_cast<unsigned char>(c) < 0x20) fail("unescaped control character");
            if (c != '\\') {
                result += c;
                continue;
            }
            switch (consume()) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    if (position_ + 4 > source_.size()) fail("truncated Unicode escape");
                    unsigned point = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char digit = consume();
                        point <<= 4;
                        if (digit >= '0' && digit <= '9') point += static_cast<unsigned>(digit - '0');
                        else if (digit >= 'a' && digit <= 'f') point += static_cast<unsigned>(digit - 'a' + 10);
                        else if (digit >= 'A' && digit <= 'F') point += static_cast<unsigned>(digit - 'A' + 10);
                        else fail("invalid Unicode escape");
                    }
                    if (point <= 0x7f) result += static_cast<char>(point);
                    else if (point <= 0x7ff) {
                        result += static_cast<char>(0xc0 | (point >> 6));
                        result += static_cast<char>(0x80 | (point & 0x3f));
                    } else {
                        result += static_cast<char>(0xe0 | (point >> 12));
                        result += static_cast<char>(0x80 | ((point >> 6) & 0x3f));
                        result += static_cast<char>(0x80 | (point & 0x3f));
                    }
                    break;
                }
                default: fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }
    JsonValue parseNumber() {
        const auto start = position_;
        if (position_ < source_.size() && source_[position_] == '-') ++position_;
        if (position_ < source_.size() && source_[position_] == '0') ++position_;
        else {
            if (position_ >= source_.size() || source_[position_] < '1' || source_[position_] > '9') fail("invalid number");
            while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_;
        }
        if (position_ < source_.size() && source_[position_] == '.') {
            ++position_;
            if (position_ >= source_.size() || source_[position_] < '0' || source_[position_] > '9') fail("invalid decimal number");
            while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_;
        }
        if (position_ < source_.size() && (source_[position_] == 'e' || source_[position_] == 'E')) {
            ++position_;
            if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-')) ++position_;
            if (position_ >= source_.size() || source_[position_] < '0' || source_[position_] > '9') fail("invalid number exponent");
            while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_;
        }
        const auto spelling = source_.substr(start, position_ - start);
        std::size_t consumed = 0;
        const double value = std::stod(spelling, &consumed);
        if (consumed != spelling.size() || !std::isfinite(value)) fail("number is not finite");
        return {{value}};
    }
    JsonValue parseValue() {
        if (++nodes_ > 20000) fail("manifest contains more than 20,000 values");
        whitespace();
        if (position_ >= source_.size()) fail("expected a JSON value");
        const char first = source_[position_];
        if (first == '"') return {{parseString()}};
        if (first == '{') {
            if (++depth_ > 64) fail("nesting exceeds 64 levels");
            ++position_;
            JsonValue::Object object;
            if (consumeIf('}')) { --depth_; return {{std::move(object)}}; }
            while (true) {
                const auto key = parseString();
                if (!consumeIf(':')) fail("expected a colon after an object key");
                if (!object.emplace(key, parseValue()).second) fail("duplicate object keys are not allowed");
                if (consumeIf('}')) { --depth_; return {{std::move(object)}}; }
                if (!consumeIf(',')) fail("expected a comma in object");
            }
        }
        if (first == '[') {
            if (++depth_ > 64) fail("nesting exceeds 64 levels");
            ++position_;
            JsonValue::Array array;
            if (consumeIf(']')) { --depth_; return {{std::move(array)}}; }
            while (true) {
                array.push_back(parseValue());
                if (consumeIf(']')) { --depth_; return {{std::move(array)}}; }
                if (!consumeIf(',')) fail("expected a comma in array");
            }
        }
        if (source_.compare(position_, 4, "true") == 0) { position_ += 4; return {{true}}; }
        if (source_.compare(position_, 5, "false") == 0) { position_ += 5; return {{false}}; }
        if (source_.compare(position_, 4, "null") == 0) { position_ += 4; return {{nullptr}}; }
        if (first == '-' || (first >= '0' && first <= '9')) return parseNumber();
        fail("unexpected character");
    }
};

struct ExtensionManifest {
    std::string id, name, version, kind, author, description;
    std::optional<std::string> waveform;
    std::optional<float> volume, cutoff, resonance, delay, chorus;
};

inline const std::string& requireString(const JsonValue& object, const char* field) {
    const auto* value = object.find(field);
    if (!value || !std::get_if<std::string>(&value->value))
        throw std::runtime_error(std::string("Extension manifest requires string field: ") + field);
    return std::get<std::string>(value->value);
}
inline std::optional<float> optionalNumber(const JsonValue& object, const char* field) {
    const auto* value = object.find(field);
    if (!value) return std::nullopt;
    const auto* number = std::get_if<double>(&value->value);
    if (!number || !std::isfinite(*number) || *number < 0.0 || *number > 1.0)
        throw std::runtime_error(std::string("Preset parameter must be a number between 0 and 1: ") + field);
    return static_cast<float>(*number);
}

struct Voice {
    double phase = 0.0, increment = 0.0;
    int note = -1;
    float velocity = 0.0f, envelope = 0.0f;
    bool active = false, releasing = false;
};

class AudioEngine {
public:
    void prepare(double sampleRate, std::size_t maxBlockSize = 2048) {
        std::lock_guard<std::mutex> lock(mutex_);
        sampleRate_ = std::max(8000.0, sampleRate);
        const auto capacity = static_cast<std::size_t>(sampleRate_ * 2.0) + maxBlockSize + 2;
        delayLeft_.assign(capacity, 0.0f);
        delayRight_.assign(capacity, 0.0f);
        delayPosition_ = 0;
        chorusPhase_ = 0.0;
    }
    void setParameter(const std::string& name, float value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (name == "volume") volume_ = clamp01(value);
        else if (name == "cutoff") cutoff_ = clamp01(value);
        else if (name == "resonance") resonance_ = std::max(0.0f, std::min(value, 0.95f));
        else if (name == "delay") delayMix_ = clamp01(value);
        else if (name == "chorus") chorusMix_ = clamp01(value);
        else if (name == "waveform") waveform_ = std::max(0, std::min(static_cast<int>(value), 2));
    }
    void noteOn(int note, float velocity) {
        if (note < 0 || note > 127 || velocity <= 0.0f) return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto* selected = &voices_[0];
        for (auto& voice : voices_) {
            if (!voice.active || voice.releasing) { selected = &voice; break; }
            if (voice.envelope < selected->envelope) selected = &voice;
        }
        *selected = Voice{};
        selected->active = true;
        selected->velocity = clamp01(velocity);
        selected->note = note;
        selected->increment = midiToFrequency(note) / sampleRate_;
    }
    void noteOff(int note) {
        if (note < 0 || note > 127) return;
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& voice : voices_)
            if (voice.active && voice.note == note) voice.releasing = true;
    }
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& voice : voices_) voice = Voice{};
        std::fill(delayLeft_.begin(), delayLeft_.end(), 0.0f);
        std::fill(delayRight_.begin(), delayRight_.end(), 0.0f);
        delayPosition_ = 0;
        filterState_ = {{0.0f, 0.0f}};
        previousFilter_ = {{0.0f, 0.0f}};
        chorusPhase_ = 0.0;
    }
    void applyPreset(const ExtensionManifest& preset) {
        const auto apply = [this](const char* name, const std::optional<float>& value) {
            if (value) setParameter(name, *value);
        };
        if (preset.waveform)
            setParameter("waveform", *preset.waveform == "saw" ? 1.0f : (*preset.waveform == "pulse" ? 2.0f : 0.0f));
        apply("volume", preset.volume);
        apply("cutoff", preset.cutoff);
        apply("resonance", preset.resonance);
        apply("delay", preset.delay);
        apply("chorus", preset.chorus);
    }
    void process(float* const* channels, std::size_t channelCount, std::size_t samples) {
        if (!channels || channelCount == 0 || channels[0] == nullptr) return;
        std::lock_guard<std::mutex> lock(mutex_);
        const auto delaySamples = std::min(delayLeft_.size() - 1,
            static_cast<std::size_t>(sampleRate_ * (0.12 + delayMix_ * 0.48)));
        const auto filterCoefficient = 0.015f + cutoff_ * 0.48f;
        const auto feedback = 0.12f + delayMix_ * 0.72f;
        for (std::size_t sample = 0; sample < samples; ++sample) {
            float mono = 0.0f;
            for (auto& voice : voices_) {
                if (!voice.active) continue;
                const float signal = oscillator(voice.phase);
                voice.phase += voice.increment;
                if (voice.phase >= 1.0) voice.phase -= 1.0;
                const float rate = static_cast<float>(1.0 / (sampleRate_ * (voice.releasing ? 0.23 : 0.012)));
                voice.envelope = voice.releasing ? std::max(0.0f, voice.envelope - rate)
                                                 : std::min(1.0f, voice.envelope + rate);
                if (voice.envelope <= 0.0001f && voice.releasing) { voice.active = false; continue; }
                mono += signal * voice.envelope * voice.velocity * 0.17f;
            }
            mono *= volume_;
            for (std::size_t channel = 0; channel < channelCount; ++channel) {
                if (!channels[channel]) continue;
                const float dry = channels[channel][sample] + (channel == 0 ? mono : 0.0f);
                const auto index = channel & 1U;
                filterState_[index] += filterCoefficient * (dry - filterState_[index]);
                float output = filterState_[index] + resonance_ * (filterState_[index] - previousFilter_[index]);
                previousFilter_[index] = filterState_[index];
                if (delaySamples > 0) {
                    const auto read = (delayPosition_ + delayLeft_.size() - delaySamples) % delayLeft_.size();
                    auto& ring = channel == 1 ? delayRight_ : delayLeft_;
                    output += ring[read] * delayMix_;
                    ring[delayPosition_] = output * feedback;
                }
                if (chorusMix_ > 0.001f && channelCount > 1) {
                    const float modulation = static_cast<float>(0.7 + 0.3 * std::sin(chorusPhase_ + channel * kPi));
                    output = output * (1.0f - chorusMix_ * 0.45f) +
                             std::tanh(output * (1.25f + modulation)) * chorusMix_ * 0.45f;
                }
                channels[channel][sample] = std::tanh(output);
            }
            delayPosition_ = (delayPosition_ + 1) % delayLeft_.size();
            chorusPhase_ += 2.0 * static_cast<double>(kPi) * 0.21 / sampleRate_;
            if (chorusPhase_ >= 2.0 * kPi) chorusPhase_ -= 2.0 * kPi;
        }
    }
private:
    float oscillator(double phase) const {
        if (waveform_ == 1) return static_cast<float>(2.0 * phase - 1.0);
        if (waveform_ == 2) return phase < 0.5 ? 0.82f : -0.82f;
        return std::sin(static_cast<float>(phase * 2.0 * kPi));
    }
    mutable std::mutex mutex_;
    std::array<Voice, kMaxVoices> voices_{};
    double sampleRate_ = 44100.0;
    float volume_ = 0.78f, cutoff_ = 0.72f, resonance_ = 0.12f;
    float delayMix_ = 0.12f, chorusMix_ = 0.08f;
    int waveform_ = 0;
    std::array<float, 2> filterState_{{0.0f, 0.0f}}, previousFilter_{{0.0f, 0.0f}};
    std::vector<float> delayLeft_ = std::vector<float>(88202, 0.0f);
    std::vector<float> delayRight_ = std::vector<float>(88202, 0.0f);
    std::size_t delayPosition_ = 0;
    double chorusPhase_ = 0.0;
};

inline ExtensionManifest parseManifest(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not open extension manifest");
    const std::string source((std::istreambuf_iterator<char>(file)), {});
    const auto root = JsonParser(source).parse();
    if (!std::get_if<JsonValue::Object>(&root.value))
        throw std::runtime_error("Extension manifest must be a JSON object");
    const auto* schema = root.find("schemaVersion");
    const auto* version = schema ? std::get_if<double>(&schema->value) : nullptr;
    if (!version || *version != 1.0) throw std::runtime_error("Extension schemaVersion must be 1");
    ExtensionManifest result;
    result.id = requireString(root, "id");
    result.name = requireString(root, "name");
    result.version = requireString(root, "version");
    result.kind = requireString(root, "kind");
    if (const auto* field = root.find("author")) {
        const auto* value = std::get_if<std::string>(&field->value);
        if (!value) throw std::runtime_error("Extension author must be a string");
        result.author = *value;
    }
    if (const auto* field = root.find("description")) {
        const auto* value = std::get_if<std::string>(&field->value);
        if (!value) throw std::runtime_error("Extension description must be a string");
        result.description = *value;
    }
    if (result.kind != "preset" && result.kind != "sample-pack" && result.kind != "native-plugin")
        throw std::runtime_error("Extension kind must be preset, sample-pack, or native-plugin");
    if (const auto* preset = root.find("preset")) {
        if (!std::get_if<JsonValue::Object>(&preset->value))
            throw std::runtime_error("Extension preset must be a JSON object");
        if (const auto* field = preset->find("waveform")) {
            const auto* wave = std::get_if<std::string>(&field->value);
            if (!wave || (*wave != "sine" && *wave != "saw" && *wave != "pulse"))
                throw std::runtime_error("Preset waveform must be sine, saw, or pulse");
            result.waveform = *wave;
        }
        result.volume = optionalNumber(*preset, "volume");
        result.cutoff = optionalNumber(*preset, "cutoff");
        result.resonance = optionalNumber(*preset, "resonance");
        result.delay = optionalNumber(*preset, "delay");
        result.chorus = optionalNumber(*preset, "chorus");
        if (result.resonance && *result.resonance > 0.95f)
            throw std::runtime_error("Preset resonance must be between 0 and 0.95");
    }
    return result;
}

inline std::vector<ExtensionManifest> scanExtensions(const std::filesystem::path& directory) {
    std::vector<ExtensionManifest> result;
    if (!std::filesystem::exists(directory)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_directory() || entry.is_symlink()) continue;
        const auto manifest = entry.path() / "extension.json";
        if (std::filesystem::exists(manifest)) result.push_back(parseManifest(manifest));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    return result;
}

} // namespace xstnsions
