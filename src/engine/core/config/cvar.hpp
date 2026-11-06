#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "engine/core/containers/result.hpp"

namespace Shard::Engine::Core {

    // Later layers win: Default, Engine, Project, User, CommandLine, Runtime (console, Set). Reset drops Runtime.
    enum class ConfigLayer : uint8_t { Default, Engine, Project, User, CommandLine, Runtime };
    inline constexpr size_t kConfigLayerCount = 6;
    const char* ConfigLayerName(ConfigLayer layer);

    enum class CVarFlags : uint32_t {
        None = 0,
        ReadOnly = 1 << 0,
        Archive = 1 << 1,
        Hidden = 1 << 2,
    };
    constexpr CVarFlags operator|(CVarFlags a, CVarFlags b) { return CVarFlags(uint32_t(a) | uint32_t(b)); }
    constexpr bool HasFlag(CVarFlags flags, CVarFlags flag) { return (uint32_t(flags) & uint32_t(flag)) != 0; }

    class CVarBase {
    public:
        using ChangeFn = std::function<void(const CVarBase&)>;

        virtual ~CVarBase();
        CVarBase(const CVarBase&) = delete;
        CVarBase& operator=(const CVarBase&) = delete;

        const std::string& Name() const { return m_Name; }
        const std::string& Description() const { return m_Description; }
        CVarFlags Flags() const { return m_Flags; }

        virtual const char* TypeName() const = 0;
        virtual std::string ToString() const = 0;
        virtual std::string DefaultString() const = 0;
        virtual bool FromString(std::string_view text, std::string* error) = 0;
        virtual void ResetToDefault() = 0;

        bool IsDefault() const { return ToString() == DefaultString(); }

        void AddOnChange(ChangeFn fn);

    protected:
        CVarBase(std::string name, std::string description, CVarFlags flags);
        void NotifyChanged();

    private:
        std::string m_Name;
        std::string m_Description;
        CVarFlags m_Flags;
        std::mutex m_CallbackMutex;
        std::vector<ChangeFn> m_Callbacks;
    };

    namespace Detail {
        template <typename T> struct CVarTraits;

        template <> struct CVarTraits<bool> {
            static constexpr const char* kName = "bool";
            static std::string Format(bool v) { return v ? "true" : "false"; }
            static bool Parse(std::string_view text, bool& out) {
                std::string s(text);
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                if (s == "1" || s == "true" || s == "on" || s == "yes") { out = true; return true; }
                if (s == "0" || s == "false" || s == "off" || s == "no") { out = false; return true; }
                return false;
            }
        };

        template <> struct CVarTraits<int> {
            static constexpr const char* kName = "int";
            static std::string Format(int v) { return std::to_string(v); }
            static bool Parse(std::string_view text, int& out) {
                const std::string s(text);
                if (s.empty()) return false;
                char* end = nullptr;
                errno = 0;
                const long v = std::strtol(s.c_str(), &end, 10);
                if (errno != 0 || end != s.c_str() + s.size()) return false;
                out = static_cast<int>(std::clamp<long>(v, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
                return true;
            }
        };

        template <> struct CVarTraits<float> {
            static constexpr const char* kName = "float";
            static std::string Format(float v) {
                // Shortest text that reads back as the same float. std::to_string saved 1e-7 as "0", and IsDefault() and the archive compare these strings.
                char buffer[32];
                for (int precision = 6; precision <= 9; ++precision) {
                    std::snprintf(buffer, sizeof(buffer), "%.*g", precision, static_cast<double>(v));
                    if (std::strtof(buffer, nullptr) == v) break;
                }
                return buffer;
            }
            static bool Parse(std::string_view text, float& out) {
                const std::string s(text);
                if (s.empty()) return false;
                char* end = nullptr;
                errno = 0;
                const float v = std::strtof(s.c_str(), &end);
                // strtof accepts "nan" and "inf", which std::clamp lets through the range
                if (errno == ERANGE || end != s.c_str() + s.size() || !std::isfinite(v)) return false;
                out = v;
                return true;
            }
        };

        template <> struct CVarTraits<std::string> {
            static constexpr const char* kName = "string";
            static std::string Format(const std::string& v) { return v; }
            static bool Parse(std::string_view text, std::string& out) { out = std::string(text); return true; }
        };

        template <typename T, bool = std::is_arithmetic_v<T>>
        struct CVarStorage {
            explicit CVarStorage(T v) : value(v) {}
            T Load() const { return value.load(std::memory_order_relaxed); }
            void Store(const T& v) { value.store(v, std::memory_order_relaxed); }
            std::atomic<T> value;
        };

        template <typename T>
        struct CVarStorage<T, false> {
            explicit CVarStorage(T v) : value(std::move(v)) {}
            T Load() const { std::lock_guard<std::mutex> lock(mutex); return value; }
            void Store(const T& v) { std::lock_guard<std::mutex> lock(mutex); value = v; }
            mutable std::mutex mutex;
            T value;
        };
    }

    class CVarRegistry;

    // Define CVars next to the code that reads them. The linker can drop an unreferenced one from a static library.
    template <typename T>
    class CVar final : public CVarBase {
        static_assert(std::is_same_v<T, bool> || std::is_same_v<T, int> || std::is_same_v<T, float> || std::is_same_v<T, std::string>,
                      "CVar supports bool, int, float and std::string");
        using Traits = Detail::CVarTraits<T>;

    public:
        CVar(std::string name, T defaultValue, std::string description = {}, CVarFlags flags = CVarFlags::None,
             T min = DefaultMin(), T max = DefaultMax());
        // A plugin's variables go to the host's registry, not the copy its own binary carries
        CVar(CVarRegistry& registry, std::string name, T defaultValue, std::string description = {}, CVarFlags flags = CVarFlags::None,
             T min = DefaultMin(), T max = DefaultMax());
        ~CVar() override;

        T Get() const { return m_Storage.Load(); }
        operator T() const { return Get(); }

        bool Set(const T& value);

        void ForceSet(const T& value) { Apply(value); }

        const char* TypeName() const override { return Traits::kName; }
        std::string ToString() const override { return Traits::Format(Get()); }
        std::string DefaultString() const override { return Traits::Format(m_Default); }
        bool FromString(std::string_view text, std::string* error) override;
        void ResetToDefault() override { Apply(m_Default); }

        const T& DefaultValue() const { return m_Default; }

    private:
        static T DefaultMin() {
            if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) return std::numeric_limits<T>::lowest();
            else return T();
        }
        static T DefaultMax() {
            if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) return std::numeric_limits<T>::max();
            else return T();
        }

        void Apply(T value) {
            if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) value = std::clamp(value, m_Min, m_Max);
            const bool changed = !(m_Storage.Load() == value);
            m_Storage.Store(value);
            if (changed) NotifyChanged();
        }

        Detail::CVarStorage<T> m_Storage;
        T m_Default;
        T m_Min, m_Max;
        CVarRegistry* m_Registry;
    };

    struct CommandResult {
        bool ok = true;
        std::string output;
    };

    class CVarRegistry {
    public:
        using CommandFn = std::function<CommandResult(const std::vector<std::string>& args)>;

        CVarRegistry();
        ~CVarRegistry();
        CVarRegistry(const CVarRegistry&) = delete;
        CVarRegistry& operator=(const CVarRegistry&) = delete;

        static CVarRegistry& Global();

        CVarBase* Find(std::string_view name) const;
        std::vector<CVarBase*> List(std::string_view prefix = {}) const;
        std::vector<std::string> Complete(std::string_view prefix) const;

        struct Suggestion {
            enum class Kind : uint8_t { Command, Variable };
            Kind kind = Kind::Command;
            std::string name;
            std::string help;
            std::string type;
            std::string value;
            std::string defaultValue;
            bool readOnly = false;
        };

        std::vector<Suggestion> Suggest(std::string_view text, size_t max = 64, bool commands = true, bool variables = true) const;

        std::optional<Suggestion> Describe(std::string_view name) const;

        void Register(CVarBase* cvar);
        void Unregister(CVarBase* cvar);

        bool SetLayerValue(ConfigLayer layer, std::string_view name, std::string_view value, std::string* error = nullptr);
        bool ClearLayerValue(ConfigLayer layer, std::string_view name);
        void ClearLayer(ConfigLayer layer);
        std::optional<std::string> GetLayerValue(ConfigLayer layer, std::string_view name) const;
        ConfigLayer WinningLayer(std::string_view name) const;
        bool Reset(std::string_view name);

        size_t LoadText(ConfigLayer layer, std::string_view text, std::vector<std::string>* warnings = nullptr);
        Result<size_t> LoadFile(ConfigLayer layer, const std::string& path, std::vector<std::string>* warnings = nullptr);
        Result<size_t> SaveArchive(const std::string& path) const;
        std::string ArchiveText() const;
        size_t ParseCommandLine(int argc, const char* const* argv, std::vector<std::string>* warnings = nullptr);

        void RegisterCommand(std::string name, std::string help, CommandFn fn);
        void UnregisterCommand(std::string_view name);
        CommandResult Execute(std::string_view line);

        static std::vector<std::string> Tokenize(std::string_view line);

    private:
        struct Command {
            std::string name;
            std::string help;
            CommandFn fn;
        };
        using LayerValues = std::array<std::optional<std::string>, kConfigLayerCount>;

        static std::string Lower(std::string_view s);
        bool ApplyWinning(const std::string& key, std::string* error);
        void RegisterBuiltinCommands();

        mutable std::mutex m_Mutex;
        std::vector<std::pair<std::string, CVarBase*>> m_CVars;
        std::vector<std::pair<std::string, LayerValues>> m_Layers;
        std::vector<std::pair<std::string, Command>> m_Commands;
    };

    template <typename T>
    CVar<T>::CVar(std::string name, T defaultValue, std::string description, CVarFlags flags, T min, T max)
        : CVarBase(std::move(name), std::move(description), flags),
          m_Storage(defaultValue), m_Default(defaultValue), m_Min(min), m_Max(max), m_Registry(&CVarRegistry::Global()) {
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            m_Default = std::clamp(m_Default, m_Min, m_Max);
            m_Storage.Store(m_Default);
        }
        m_Registry->Register(this);
    }

    template <typename T>
    CVar<T>::CVar(CVarRegistry& registry, std::string name, T defaultValue, std::string description, CVarFlags flags, T min, T max)
        : CVarBase(std::move(name), std::move(description), flags),
          m_Storage(defaultValue), m_Default(defaultValue), m_Min(min), m_Max(max), m_Registry(&registry) {
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            m_Default = std::clamp(m_Default, m_Min, m_Max);
            m_Storage.Store(m_Default);
        }
        m_Registry->Register(this);
    }

    template <typename T>
    CVar<T>::~CVar() { m_Registry->Unregister(this); }

    template <typename T>
    bool CVar<T>::FromString(std::string_view text, std::string* error) {
        T value{};
        if (!Traits::Parse(text, value)) {
            if (error) *error = "'" + std::string(text) + "' is not a valid " + Traits::kName + " for " + Name();
            return false;
        }
        Apply(std::move(value));
        return true;
    }

    template <typename T>
    bool CVar<T>::Set(const T& value) {
        return m_Registry->SetLayerValue(ConfigLayer::Runtime, Name(), Traits::Format(value));
    }
}
