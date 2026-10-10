#include "engine/core/config/cvar.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace Shard::Engine::Core {

    const char* ConfigLayerName(ConfigLayer layer) {
        switch (layer) {
            case ConfigLayer::Default:     return "default";
            case ConfigLayer::Engine:      return "engine";
            case ConfigLayer::Project:     return "project";
            case ConfigLayer::User:        return "user";
            case ConfigLayer::CommandLine: return "command line";
            case ConfigLayer::Runtime:     return "runtime";
        }
        return "?";
    }

    // ---------------------------------------------------------------------------------------------------- CVarBase

    CVarBase::CVarBase(std::string name, std::string description, CVarFlags flags)
        : m_Name(std::move(name)), m_Description(std::move(description)), m_Flags(flags) {}

    CVarBase::~CVarBase() = default;

    void CVarBase::AddOnChange(ChangeFn fn) {
        std::lock_guard<std::mutex> lock(m_CallbackMutex);
        m_Callbacks.push_back(std::move(fn));
    }

    void CVarBase::NotifyChanged() {
        std::vector<ChangeFn> callbacks;
        {
            std::lock_guard<std::mutex> lock(m_CallbackMutex);
            callbacks = m_Callbacks;                    // call outside the lock : a callback may add another one
        }
        for (const ChangeFn& fn : callbacks) fn(*this);
    }

    // ------------------------------------------------------------------------------------------------ CVarRegistry

    namespace {
        template <typename V>
        auto FindEntry(V& vec, const std::string& key) -> decltype(vec.begin()) {
            return std::find_if(vec.begin(), vec.end(), [&](const auto& e) { return e.first == key; });
        }

        std::string Trim(std::string_view s) {
            size_t b = 0, e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
            return std::string(s.substr(b, e - b));
        }
    }

    CVarRegistry::CVarRegistry() { RegisterBuiltinCommands(); }
    CVarRegistry::~CVarRegistry() = default;

    CVarRegistry& CVarRegistry::Global() {
        static CVarRegistry registry;
        return registry;
    }

    std::string CVarRegistry::Lower(std::string_view s) {
        std::string out(s);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return out;
    }

    CVarBase* CVarRegistry::Find(std::string_view name) const {
        const std::string key = Lower(name);
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = FindEntry(m_CVars, key);
        return it != m_CVars.end() ? it->second : nullptr;
    }

    std::vector<CVarBase*> CVarRegistry::List(std::string_view prefix) const {
        const std::string p = Lower(prefix);
        std::vector<std::pair<std::string, CVarBase*>> found;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            for (const auto& entry : m_CVars)
                if (entry.first.compare(0, p.size(), p) == 0 && !HasFlag(entry.second->Flags(), CVarFlags::Hidden))
                    found.push_back(entry);
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<CVarBase*> out;
        for (auto& e : found) out.push_back(e.second);
        return out;
    }

    std::vector<std::string> CVarRegistry::Complete(std::string_view prefix) const {
        const std::string p = Lower(prefix);
        std::vector<std::string> out;
        std::lock_guard<std::mutex> lock(m_Mutex);
        for (const auto& entry : m_CVars)
            if (entry.first.compare(0, p.size(), p) == 0 && !HasFlag(entry.second->Flags(), CVarFlags::Hidden))
                out.push_back(entry.second->Name());
        for (const auto& entry : m_Commands)
            if (entry.first.compare(0, p.size(), p) == 0) out.push_back(entry.second.name);
        std::sort(out.begin(), out.end());
        return out;
    }

    void CVarRegistry::Register(CVarBase* cvar) {
        const std::string key = Lower(cvar->Name());
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = FindEntry(m_CVars, key);
            if (it != m_CVars.end()) {
                return;                                 // two variables with one name is a programming error : the first keeps it
            }
            m_CVars.emplace_back(key, cvar);
        }
        ApplyWinning(key, nullptr);                     // a config file may have been loaded before this variable existed
    }

    void CVarRegistry::Unregister(CVarBase* cvar) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_CVars.erase(std::remove_if(m_CVars.begin(), m_CVars.end(),
                                     [&](const auto& e) { return e.second == cvar; }), m_CVars.end());
    }

    bool CVarRegistry::ApplyWinning(const std::string& key, std::string* error) {
        CVarBase* cvar = nullptr;
        std::optional<std::string> value;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = FindEntry(m_CVars, key);
            if (it == m_CVars.end()) return true;       // nothing to apply to yet : the value waits in its layer
            cvar = it->second;
            auto layers = FindEntry(m_Layers, key);
            if (layers != m_Layers.end())
                for (size_t i = kConfigLayerCount; i-- > 0;)
                    if (layers->second[i]) { value = layers->second[i]; break; }
        }
        // Outside the lock : changing the value runs the variable's callbacks, which may use the registry
        if (!value) { cvar->ResetToDefault(); return true; }
        return cvar->FromString(*value, error);
    }

    bool CVarRegistry::SetLayerValue(ConfigLayer layer, std::string_view name, std::string_view value, std::string* error) {
        const std::string key = Lower(name);

        CVarBase* cvar = Find(name);
        if (cvar) {
            if (layer == ConfigLayer::Runtime && HasFlag(cvar->Flags(), CVarFlags::ReadOnly)) {
                if (error) *error = cvar->Name() + " is read-only";
                return false;
            }
        }

        std::optional<std::string> previous;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = FindEntry(m_Layers, key);
            if (it == m_Layers.end()) {
                m_Layers.emplace_back(key, LayerValues{});
                it = m_Layers.end() - 1;
            }
            previous = it->second[static_cast<size_t>(layer)];
            it->second[static_cast<size_t>(layer)] = std::string(value);
        }

        std::string applyError;
        if (!ApplyWinning(key, &applyError)) {
            // The value is not valid for this variable : do not keep it, put back what was there
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                auto it = FindEntry(m_Layers, key);
                if (it != m_Layers.end()) it->second[static_cast<size_t>(layer)] = previous;
            }
            ApplyWinning(key, nullptr);
            if (error) *error = applyError;
            return false;
        }
        return true;
    }

    bool CVarRegistry::ClearLayerValue(ConfigLayer layer, std::string_view name) {
        const std::string key = Lower(name);
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = FindEntry(m_Layers, key);
            if (it == m_Layers.end() || !it->second[static_cast<size_t>(layer)]) return false;
            it->second[static_cast<size_t>(layer)].reset();
        }
        ApplyWinning(key, nullptr);
        return true;
    }

    void CVarRegistry::ClearLayer(ConfigLayer layer) {
        std::vector<std::string> keys;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            for (auto& entry : m_Layers)
                if (entry.second[static_cast<size_t>(layer)]) {
                    entry.second[static_cast<size_t>(layer)].reset();
                    keys.push_back(entry.first);
                }
        }
        for (const std::string& key : keys) ApplyWinning(key, nullptr);
    }

    std::optional<std::string> CVarRegistry::GetLayerValue(ConfigLayer layer, std::string_view name) const {
        const std::string key = Lower(name);
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = FindEntry(m_Layers, key);
        return it != m_Layers.end() ? it->second[static_cast<size_t>(layer)] : std::nullopt;
    }

    ConfigLayer CVarRegistry::WinningLayer(std::string_view name) const {
        const std::string key = Lower(name);
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = FindEntry(m_Layers, key);
        if (it != m_Layers.end())
            for (size_t i = kConfigLayerCount; i-- > 0;)
                if (it->second[i]) return static_cast<ConfigLayer>(i);
        return ConfigLayer::Default;
    }

    bool CVarRegistry::Reset(std::string_view name) {
        if (!Find(name)) return false;
        ClearLayerValue(ConfigLayer::Runtime, name);
        return true;
    }

    // ----------------------------------------------------------------------------------------------------- Files

    size_t CVarRegistry::LoadText(ConfigLayer layer, std::string_view text, std::vector<std::string>* warnings) {
        size_t applied = 0, lineNumber = 0;
        std::string section;
        std::istringstream stream{std::string(text)};
        std::string raw;
        while (std::getline(stream, raw)) {
            ++lineNumber;
            const std::string line = Trim(raw);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;

            if (line.front() == '[') {
                if (line.back() != ']') {
                    if (warnings) warnings->push_back("line " + std::to_string(lineNumber) + ": unterminated section header");
                    continue;
                }
                section = Trim(std::string_view(line).substr(1, line.size() - 2));
                continue;
            }

            const size_t eq = line.find('=');
            if (eq == std::string::npos) {
                if (warnings) warnings->push_back("line " + std::to_string(lineNumber) + ": expected 'key = value'");
                continue;
            }
            std::string key = Trim(std::string_view(line).substr(0, eq));
            std::string value = Trim(std::string_view(line).substr(eq + 1));
            if (key.empty()) {
                if (warnings) warnings->push_back("line " + std::to_string(lineNumber) + ": empty key");
                continue;
            }
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
            if (!section.empty()) key = section + "." + key;

            std::string error;
            if (SetLayerValue(layer, key, value, &error)) ++applied;
            else if (warnings) warnings->push_back("line " + std::to_string(lineNumber) + ": " + error);
        }
        return applied;
    }

    Result<size_t> CVarRegistry::LoadFile(ConfigLayer layer, const std::string& path, std::vector<std::string>* warnings) {
        std::ifstream file(path);
        if (!file) return Err("can't open " + path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return Ok(LoadText(layer, buffer.str(), warnings));
    }

    std::string CVarRegistry::ArchiveText() const {
        std::vector<std::pair<std::string, std::string>> lines;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            for (const auto& entry : m_CVars) {
                const CVarBase* cvar = entry.second;
                if (!HasFlag(cvar->Flags(), CVarFlags::Archive) || cvar->IsDefault()) continue;

                // Only what the user chose (the console, or the user file itself) : a value that comes from the engine
                // or project files, or from this run's command line, must not become the user's setting
                auto layers = FindEntry(m_Layers, entry.first);
                if (layers == m_Layers.end()) continue;
                size_t winning = 0;
                for (size_t i = kConfigLayerCount; i-- > 0;)
                    if (layers->second[i]) { winning = i; break; }
                if (winning == static_cast<size_t>(ConfigLayer::User) || winning == static_cast<size_t>(ConfigLayer::Runtime))
                    lines.emplace_back(cvar->Name(), cvar->ToString());
            }
        }
        std::sort(lines.begin(), lines.end());
        std::string out = "# Saved settings (name = value). Edit freely.\n";
        for (const auto& [name, value] : lines) out += name + " = " + value + "\n";
        return out;
    }

    Result<size_t> CVarRegistry::SaveArchive(const std::string& path) const {
        const std::string text = ArchiveText();
        std::ofstream file(path, std::ios::trunc);
        if (!file) return Err("can't open " + path + " for writing");
        file << text;
        file.close();
        if (!file) return Err("can't write " + path);
        return Ok(static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) - 1);
    }

    size_t CVarRegistry::ParseCommandLine(int argc, const char* const* argv, std::vector<std::string>* warnings) {
        size_t applied = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg.size() < 2 || arg[0] != '+') continue;
            std::string name, value;
            const size_t eq = arg.find('=');
            if (eq != std::string::npos) {
                name = arg.substr(1, eq - 1);
                value = arg.substr(eq + 1);
            } else if (i + 1 < argc) {
                name = arg.substr(1);
                value = argv[++i];
            } else {
                if (warnings) warnings->push_back("'" + arg + "' has no value");
                continue;
            }
            std::string error;
            if (SetLayerValue(ConfigLayer::CommandLine, name, value, &error)) ++applied;
            else if (warnings) warnings->push_back(error);
        }
        return applied;
    }

    // -------------------------------------------------------------------------------------------------- Commands

    void CVarRegistry::RegisterCommand(std::string name, std::string help, CommandFn fn) {
        const std::string key = Lower(name);
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = FindEntry(m_Commands, key);
        Command command{std::move(name), std::move(help), std::move(fn)};
        if (it != m_Commands.end()) it->second = std::move(command);
        else m_Commands.emplace_back(key, std::move(command));
    }

    void CVarRegistry::UnregisterCommand(std::string_view name) {
        const std::string key = Lower(name);
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Commands.erase(std::remove_if(m_Commands.begin(), m_Commands.end(),
                                        [&](const auto& e) { return e.first == key; }), m_Commands.end());
    }

    std::vector<std::string> CVarRegistry::Tokenize(std::string_view line) {
        std::vector<std::string> tokens;
        std::string current;
        bool inQuotes = false, hasToken = false;
        for (char c : line) {
            if (c == '"') { inQuotes = !inQuotes; hasToken = true; continue; }
            if (!inQuotes && std::isspace(static_cast<unsigned char>(c))) {
                if (hasToken) { tokens.push_back(current); current.clear(); hasToken = false; }
                continue;
            }
            current.push_back(c);
            hasToken = true;
        }
        if (hasToken) tokens.push_back(current);
        return tokens;
    }

    CommandResult CVarRegistry::Execute(std::string_view line) {
        std::vector<std::string> tokens = Tokenize(line);
        if (tokens.empty()) return {true, {}};

        const std::string head = tokens[0];
        const std::string key = Lower(head);
        std::vector<std::string> args(tokens.begin() + 1, tokens.end());

        CommandFn command;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = FindEntry(m_Commands, key);
            if (it != m_Commands.end()) command = it->second.fn;
        }
        if (command) return command(args);              // outside the lock : a command uses the registry

        CVarBase* cvar = Find(head);
        if (!cvar) return {false, "Unknown command or variable '" + head + "' (try 'help')"};

        if (args.empty()) {
            std::string out = cvar->Name() + " = " + cvar->ToString() + "  (" + cvar->TypeName() + ", default " + cvar->DefaultString() + ")";
            if (!cvar->Description().empty()) out += "\n  " + cvar->Description();
            return {true, out};
        }

        std::string value = args[0];
        for (size_t i = 1; i < args.size(); ++i) value += " " + args[i];
        std::string error;
        if (!SetLayerValue(ConfigLayer::Runtime, head, value, &error)) return {false, error};
        return {true, cvar->Name() + " = " + cvar->ToString()};
    }

    void CVarRegistry::RegisterBuiltinCommands() {
        RegisterCommand("help", "help [name] : lists the commands, or describes a command / variable",
            [this](const std::vector<std::string>& args) -> CommandResult {
                if (args.empty()) {
                    std::string out = "Commands:\n";
                    std::vector<std::string> lines;
                    {
                        std::lock_guard<std::mutex> lock(m_Mutex);
                        for (const auto& e : m_Commands) lines.push_back("  " + e.second.help);
                    }
                    std::sort(lines.begin(), lines.end());
                    for (const std::string& l : lines) out += l + "\n";
                    out += "Type a variable name to see it, 'name value' to set it, 'list [prefix]' to list them.";
                    return {true, out};
                }
                {
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    auto it = FindEntry(m_Commands, Lower(args[0]));
                    if (it != m_Commands.end()) return {true, it->second.help};
                }
                if (CVarBase* cvar = Find(args[0]))
                    return {true, cvar->Name() + " (" + cvar->TypeName() + ") " + cvar->Description()};
                return {false, "Unknown command or variable '" + args[0] + "'"};
            });

        RegisterCommand("list", "list [prefix] : lists the variables (all, or those starting with prefix)",
            [this](const std::vector<std::string>& args) -> CommandResult {
                std::string out;
                size_t count = 0;
                for (CVarBase* cvar : List(args.empty() ? std::string_view{} : std::string_view(args[0]))) {
                    out += cvar->Name() + " = " + cvar->ToString() + "\n";
                    ++count;
                }
                out += std::to_string(count) + " variable(s)";
                return {true, out};
            });

        RegisterCommand("get", "get name : shows a variable",
            [this](const std::vector<std::string>& args) -> CommandResult {
                if (args.empty()) return {false, "usage: get name"};
                CVarBase* cvar = Find(args[0]);
                if (!cvar) return {false, "Unknown variable '" + args[0] + "'"};
                return {true, cvar->Name() + " = " + cvar->ToString()};
            });

        RegisterCommand("set", "set name value : sets a variable",
            [this](const std::vector<std::string>& args) -> CommandResult {
                if (args.size() < 2) return {false, "usage: set name value"};
                CVarBase* cvar = Find(args[0]);
                if (!cvar) return {false, "Unknown variable '" + args[0] + "'"};
                std::string value = args[1];
                for (size_t i = 2; i < args.size(); ++i) value += " " + args[i];
                std::string error;
                if (!SetLayerValue(ConfigLayer::Runtime, args[0], value, &error)) return {false, error};
                return {true, cvar->Name() + " = " + cvar->ToString()};
            });

        RegisterCommand("reset", "reset name : puts a variable back to what the config files say (or its default)",
            [this](const std::vector<std::string>& args) -> CommandResult {
                if (args.empty()) return {false, "usage: reset name"};
                CVarBase* cvar = Find(args[0]);
                if (!cvar) return {false, "Unknown variable '" + args[0] + "'"};
                Reset(args[0]);
                return {true, cvar->Name() + " = " + cvar->ToString()};
            });

        RegisterCommand("toggle", "toggle name : flips a boolean variable",
            [this](const std::vector<std::string>& args) -> CommandResult {
                if (args.empty()) return {false, "usage: toggle name"};
                CVarBase* cvar = Find(args[0]);
                if (!cvar) return {false, "Unknown variable '" + args[0] + "'"};
                if (std::string(cvar->TypeName()) != "bool") return {false, cvar->Name() + " is not a bool"};
                std::string error;
                if (!SetLayerValue(ConfigLayer::Runtime, args[0], cvar->ToString() == "true" ? "false" : "true", &error))
                    return {false, error};
                return {true, cvar->Name() + " = " + cvar->ToString()};
            });

        RegisterCommand("echo", "echo text : prints text",
            [](const std::vector<std::string>& args) -> CommandResult {
                std::string out;
                for (size_t i = 0; i < args.size(); ++i) out += (i ? " " : "") + args[i];
                return {true, out};
            });

        RegisterCommand("exec", "exec file : runs the console commands of a file, one per line",
            [this](const std::vector<std::string>& args) -> CommandResult {
                if (args.empty()) return {false, "usage: exec file"};
                std::ifstream file(args[0]);
                if (!file) return {false, "can't open " + args[0]};
                std::string line, out;
                bool ok = true;
                while (std::getline(file, line)) {
                    const std::string trimmed = Trim(line);
                    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;
                    CommandResult r = Execute(trimmed);
                    ok = ok && r.ok;
                    if (!r.output.empty()) out += r.output + "\n";
                }
                return {ok, out};
            });
    }
}
