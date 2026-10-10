#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdint>
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

    /// Where a value comes from. Later layers override earlier ones : a value set on the command line wins over the
    /// user's file, which wins over the project's, which wins over the engine's, which wins over the default in the code.
    /// Runtime (the console, code calling Set) wins over everything and is dropped by Reset.
    enum class ConfigLayer : uint8_t { Default, Engine, Project, User, CommandLine, Runtime };
    inline constexpr size_t kConfigLayerCount = 6;
    const char* ConfigLayerName(ConfigLayer layer);

    enum class CVarFlags : uint32_t {
        None = 0,
        ReadOnly = 1 << 0,      ///< cannot be changed from the console or by Set() : only config files and the owner (ForceSet)
        Archive = 1 << 1,       ///< saved to the user config file when its value differs from the default
        Hidden = 1 << 2,        ///< not listed by `list` and completion (still usable by name)
    };
    constexpr CVarFlags operator|(CVarFlags a, CVarFlags b) { return CVarFlags(uint32_t(a) | uint32_t(b)); }
    constexpr bool HasFlag(CVarFlags flags, CVarFlags flag) { return (uint32_t(flags) & uint32_t(flag)) != 0; }

    /// @brief What the registry sees of a console variable, whatever its type.
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
        virtual std::string ToString() const = 0;               ///< current value, as text
        virtual std::string DefaultString() const = 0;
        /// @brief Parses `text`, applies it (clamped to the range of numeric variables). False + message if it is not a
        /// valid value of this type. Layers and ReadOnly are the registry's business, this just sets the value.
        virtual bool FromString(std::string_view text, std::string* error) = 0;
        virtual void ResetToDefault() = 0;

        bool IsDefault() const { return ToString() == DefaultString(); }

        /// @brief Called (on the thread that changed the value) after every change of value.
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
                std::string s = std::to_string(v);                    // 6 decimals : trim the zeros ("0.500000" -> "0.5")
                if (s.find('.') != std::string::npos) {
                    while (s.back() == '0') s.pop_back();
                    if (s.back() == '.') s.pop_back();
                }
                return s;
            }
            static bool Parse(std::string_view text, float& out) {
                const std::string s(text);
                if (s.empty()) return false;
                char* end = nullptr;
                errno = 0;
                const float v = std::strtof(s.c_str(), &end);
                if (errno == ERANGE || end != s.c_str() + s.size()) return false;
                out = v;
                return true;
            }
        };

        template <> struct CVarTraits<std::string> {
            static constexpr const char* kName = "string";
            static std::string Format(const std::string& v) { return v; }
            static bool Parse(std::string_view text, std::string& out) { out = std::string(text); return true; }
        };

        /// Atomic for the arithmetic types (read from any thread, e.g. by jobs), a mutex for strings.
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

    /// @brief A named, typed setting that can be read from anywhere, changed from the console or a config file, and
    /// reacted to. Declare it as a static or a member :
    ///
    ///     static CVar<int> cvIdleSpins("jobs.idle_spins", 64, "Spins before an idle worker sleeps", CVarFlags::Archive, 0, 100000);
    ///     ...
    ///     for (int i = 0; i < cvIdleSpins.Get(); ++i) ...
    ///
    /// Supported types : bool, int, float, std::string. Names are case-insensitive, dotted by convention
    /// ("system.name"). A CVar registers itself with CVarRegistry::Global() and unregisters when destroyed.
    ///
    /// A CVar defined in a translation unit that nothing else references can be dropped by the linker when it sits in
    /// a static library : define them next to the code that reads them.
    template <typename T>
    class CVar final : public CVarBase {
        static_assert(std::is_same_v<T, bool> || std::is_same_v<T, int> || std::is_same_v<T, float> || std::is_same_v<T, std::string>,
                      "CVar supports bool, int, float and std::string");
        using Traits = Detail::CVarTraits<T>;

    public:
        /// @param min,max range of numeric variables (values outside are clamped) ; ignored for bool and string.
        CVar(std::string name, T defaultValue, std::string description = {}, CVarFlags flags = CVarFlags::None,
             T min = DefaultMin(), T max = DefaultMax());
        ~CVar() override;

        T Get() const { return m_Storage.Load(); }
        operator T() const { return Get(); }

        /// @brief Sets the value on the Runtime layer, like typing it in the console. False if the variable is
        /// ReadOnly or the value is out of what it accepts.
        bool Set(const T& value);

        /// @brief The owner changing its own variable (even a ReadOnly one), bypassing the layers : to publish a
        /// computed value such as "jobs.workers".
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
    };

    /// @brief What running a console line gives back.
    struct CommandResult {
        bool ok = true;
        std::string output;             ///< text to show the user (may be empty, may span lines)
    };

    /// @brief Every CVar and console command, plus the layered values that feed the CVars.
    ///
    /// Values can arrive before the CVar exists (a config file is loaded at startup, the CVar is created by a later
    /// static initialiser or a module loaded afterwards) : the registry keeps them per layer and applies the winning one
    /// as soon as the CVar registers.
    class CVarRegistry {
    public:
        using CommandFn = std::function<CommandResult(const std::vector<std::string>& args)>;

        CVarRegistry();
        ~CVarRegistry();
        CVarRegistry(const CVarRegistry&) = delete;
        CVarRegistry& operator=(const CVarRegistry&) = delete;

        /// The process-wide registry that CVar uses.
        static CVarRegistry& Global();

        // ---- CVars ----
        CVarBase* Find(std::string_view name) const;
        /// Non-hidden variables whose name starts with `prefix`, sorted by name.
        std::vector<CVarBase*> List(std::string_view prefix = {}) const;
        /// Names (variables and commands) starting with `prefix`, sorted : for console completion.
        std::vector<std::string> Complete(std::string_view prefix) const;

        // Used by CVar's constructor and destructor
        void Register(CVarBase* cvar);
        void Unregister(CVarBase* cvar);

        // ---- Layers ----
        /// @brief Records `value` for `name` in `layer` and, if the variable exists, applies the value that now wins.
        /// The Runtime layer refuses ReadOnly variables. False + message if the value is not valid for the variable
        /// (the layer then keeps nothing for it).
        bool SetLayerValue(ConfigLayer layer, std::string_view name, std::string_view value, std::string* error = nullptr);
        /// @brief Forgets the value of `name` in `layer` and re-applies whatever wins now (or the default).
        bool ClearLayerValue(ConfigLayer layer, std::string_view name);
        void ClearLayer(ConfigLayer layer);
        std::optional<std::string> GetLayerValue(ConfigLayer layer, std::string_view name) const;
        /// The layer whose value is applied to `name` (Default when no layer has one).
        ConfigLayer WinningLayer(std::string_view name) const;
        /// @brief Drops the Runtime value : the variable goes back to what the other layers say.
        bool Reset(std::string_view name);

        // ---- Files ----
        /// @brief Reads "key = value" lines into `layer`. `# comment` and `; comment` lines are ignored, `[section]` prefixes
        /// the following keys with "section.". Returns how many values were set ; lines that are wrong are skipped and
        /// described in `warnings`.
        size_t LoadText(ConfigLayer layer, std::string_view text, std::vector<std::string>* warnings = nullptr);
        Result<size_t> LoadFile(ConfigLayer layer, const std::string& path, std::vector<std::string>* warnings = nullptr);
        /// @brief Writes the Archive variables set by the user (console, or the user file) and different from their default, as
        /// "key = value" lines. Values that come from the engine / project files or the command line are not the user's choice.
        Result<size_t> SaveArchive(const std::string& path) const;
        std::string ArchiveText() const;
        /// @brief Command line : "+name=value" or "+name value" sets `name` on the CommandLine layer. Other arguments are
        /// left alone. Returns how many were applied.
        size_t ParseCommandLine(int argc, const char* const* argv, std::vector<std::string>* warnings = nullptr);

        // ---- Commands ----
        void RegisterCommand(std::string name, std::string help, CommandFn fn);
        void UnregisterCommand(std::string_view name);
        /// @brief Runs one console line : a command with its arguments, or a variable name (shows it) or a variable name
        /// followed by a value (sets it on the Runtime layer). Quotes group words : set name "a b".
        CommandResult Execute(std::string_view line);

        /// @brief Splits a line into words ("double quotes" keep spaces together).
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
        std::vector<std::pair<std::string, CVarBase*>> m_CVars;        // lowercase name -> variable
        std::vector<std::pair<std::string, LayerValues>> m_Layers;      // lowercase name -> value per layer
        std::vector<std::pair<std::string, Command>> m_Commands;
    };

    // ---- CVar<T> out-of-line ----

    template <typename T>
    CVar<T>::CVar(std::string name, T defaultValue, std::string description, CVarFlags flags, T min, T max)
        : CVarBase(std::move(name), std::move(description), flags),
          m_Storage(defaultValue), m_Default(defaultValue), m_Min(min), m_Max(max) {
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            m_Default = std::clamp(m_Default, m_Min, m_Max);
            m_Storage.Store(m_Default);
        }
        CVarRegistry::Global().Register(this);          // applies a value that was waiting for this variable
    }

    template <typename T>
    CVar<T>::~CVar() { CVarRegistry::Global().Unregister(this); }

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
        return CVarRegistry::Global().SetLayerValue(ConfigLayer::Runtime, Name(), Traits::Format(value));
    }
}
