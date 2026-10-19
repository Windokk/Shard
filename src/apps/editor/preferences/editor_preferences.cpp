#include "editor_preferences.hpp"
#include "editor_preferences.reflection.hpp"

#include "engine/world/engine.hpp"
#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

using nlohmann::json;
using nlohmann::ordered_json;

namespace Shard::Editor::Core {

    namespace {

        // Each Read leaves `value` alone unless the key is there with the right type : a missing or broken entry keeps its default

        void Read(const json& object, const char* key, bool& value)
        {
            auto it = object.find(key);
            if(it != object.end() && it->is_boolean())
                value = it->get<bool>();
        }

        void Read(const json& object, const char* key, float& value, float min, float max)
        {
            auto it = object.find(key);
            if(it != object.end() && it->is_number())
                value = std::clamp(it->get<float>(), min, max);
        }

        void Read(const json& object, const char* key, int& value, int min, int max)
        {
            auto it = object.find(key);
            if(it != object.end() && it->is_number_integer())
                value = std::clamp(it->get<int>(), min, max);
        }

        // A reflected preference : read under its field's name, kept to its field's range (FIELD range=)

        void Read(const json& object, const FieldInfo& field, float& value)
        {
            Read(object, field.name, value, field.min, field.max);
        }

        void Read(const json& object, const FieldInfo& field, int& value)
        {
            Read(object, field.name, value, static_cast<int>(field.min), static_cast<int>(field.max));
        }

        const json& Section(const json& data, const char* key)
        {
            static const json empty = json::object();
            auto it = data.find(key);
            return (it != data.end() && it->is_object()) ? *it : empty;
        }
    }

    PreferenceRange GetPreferenceRange(const char* structName, const char* fieldName)
    {
        // The name as the macro spelled it may be qualified (Core::CameraPreferences)
        std::string name = structName;
        const size_t colon = name.rfind(':');
        if(colon != std::string::npos)
            name = name.substr(colon + 1);

        for(const StructDescriptor* descriptor : { &AppearancePreferences_descriptor, &CameraPreferences_descriptor, &SavingPreferences_descriptor })
        {
            if(descriptor->name != name)
                continue;
            for(const FieldInfo* field : descriptor->fields)
                if(std::string(field->name) == fieldName)
                    return { field->min, field->max };
        }

        DEBUG_ERROR(std::string("No range for the editor preference ") + structName + "::" + fieldName + " : give it FIELD(Editable, range=min|max)");
        return {};
    }

    std::string GetEditorPreferencesPath()
    {
        auto* fileManager = Engine::Core::GetEngine().GetFileManager();
        if(!fileManager)
            return "editor_preferences.json";
        return (fileManager->GetCurrentExecutablePath() / std::string("editor_preferences.json")).GetNativePath();
    }

    std::string SerializeEditorPreferences(const EditorPreferences& p)
    {
        ordered_json data;

        data["appearance"]["theme"] = static_cast<int>(p.appearance.theme);
        data["appearance"]["uiScale"] = p.appearance.uiScale;

        data["camera"]["speed"] = p.camera.speed;
        data["camera"]["mouseSensitivity"] = p.camera.mouseSensitivity;
        data["camera"]["invertY"] = p.camera.invertY;
        data["camera"]["fieldOfView"] = p.camera.fieldOfView;
        data["camera"]["nearPlane"] = p.camera.nearPlane;
        data["camera"]["farPlane"] = p.camera.farPlane;
        data["camera"]["focusSpeed"] = p.camera.focusSpeed;

        data["saving"]["autoSaveWorld"] = p.saving.autoSaveWorld;
        data["saving"]["autoSaveIntervalMinutes"] = p.saving.autoSaveIntervalMinutes;
        data["saving"]["undoHistorySize"] = p.saving.undoHistorySize;

        const EditorSettings& v = p.viewport;
        data["viewport"]["showOutlines"] = v.showOutlines;
        data["viewport"]["showGizmos"] = v.showGizmos;
        data["viewport"]["showDDGIGizmos"] = v.showDDGIGizmos;
        data["viewport"]["showPhysicsShapes"] = v.showPhysicsShapes;
        data["viewport"]["showBillboards"] = v.showBillboards;
        data["viewport"]["showLighting"] = v.showLighting;
        data["viewport"]["showShadows"] = v.showShadows;
        data["viewport"]["showGrid"] = v.showGrid;
        data["viewport"]["showGridX"] = v.showGridX;
        data["viewport"]["showGridY"] = v.showGridY;
        data["viewport"]["showGridZ"] = v.showGridZ;
        data["viewport"]["viewMode"] = static_cast<int>(v.viewMode);
        data["viewport"]["snapLocation"] = v.snapLocation;
        data["viewport"]["snapRotation"] = v.snapRotation;
        data["viewport"]["snapScale"] = v.snapScale;
        data["viewport"]["locationSnap"] = v.locationSnap;
        data["viewport"]["rotationSnap"] = v.rotationSnap;
        data["viewport"]["scaleSnap"] = v.scaleSnap;

        const PanelVisibility& w = p.panels;
        data["panels"]["viewport"] = w.viewport;
        data["panels"]["worldTree"] = w.worldTree;
        data["panels"]["properties"] = w.properties;
        data["panels"]["assetBrowser"] = w.assetBrowser;
        data["panels"]["console"] = w.console;
        data["panels"]["profiler"] = w.profiler;
        data["panels"]["worldSettings"] = w.worldSettings;
        data["panels"]["projectSettings"] = w.projectSettings;
        data["panels"]["editorPreferences"] = w.editorPreferences;
        data["panels"]["plugins"] = w.plugins;

        return data.dump(4);
    }

    bool LoadEditorPreferences(const std::string& path, EditorPreferences& p, std::string* fileText)
    {
        if(fileText)
            fileText->clear();

        std::ifstream file(path, std::ios::binary);
        if(!file)
            return false;

        std::stringstream buffer;
        buffer << file.rdbuf();
        if(fileText)
            *fileText = buffer.str();

        json data = json::parse(buffer.str(), nullptr, false);
        if(data.is_discarded() || !data.is_object())
        {
            DEBUG_WARNING("The editor preferences file \"" + path + "\" is not valid JSON : using the defaults");
            return false;
        }

        const json& appearance = Section(data, "appearance");
        int theme = static_cast<int>(p.appearance.theme);
        Read(appearance, "theme", theme, 0, static_cast<int>(EditorTheme::Classic));
        p.appearance.theme = static_cast<EditorTheme>(theme);
        Read(appearance, AppearancePreferences_uiScale_info, p.appearance.uiScale);

        const json& camera = Section(data, "camera");
        Read(camera, CameraPreferences_speed_info, p.camera.speed);
        Read(camera, CameraPreferences_mouseSensitivity_info, p.camera.mouseSensitivity);
        Read(camera, "invertY", p.camera.invertY);
        Read(camera, CameraPreferences_fieldOfView_info, p.camera.fieldOfView);
        Read(camera, CameraPreferences_nearPlane_info, p.camera.nearPlane);
        Read(camera, CameraPreferences_farPlane_info, p.camera.farPlane);
        p.camera.farPlane = std::max(p.camera.farPlane, p.camera.nearPlane + 0.01f);
        Read(camera, CameraPreferences_focusSpeed_info, p.camera.focusSpeed);

        const json& saving = Section(data, "saving");
        Read(saving, "autoSaveWorld", p.saving.autoSaveWorld);
        Read(saving, SavingPreferences_autoSaveIntervalMinutes_info, p.saving.autoSaveIntervalMinutes);
        Read(saving, SavingPreferences_undoHistorySize_info, p.saving.undoHistorySize);

        const json& viewport = Section(data, "viewport");
        EditorSettings& v = p.viewport;
        Read(viewport, "showOutlines", v.showOutlines);
        Read(viewport, "showGizmos", v.showGizmos);
        Read(viewport, "showDDGIGizmos", v.showDDGIGizmos);
        Read(viewport, "showPhysicsShapes", v.showPhysicsShapes);
        Read(viewport, "showBillboards", v.showBillboards);
        Read(viewport, "showLighting", v.showLighting);
        Read(viewport, "showShadows", v.showShadows);
        Read(viewport, "showGrid", v.showGrid);
        Read(viewport, "showGridX", v.showGridX);
        Read(viewport, "showGridY", v.showGridY);
        Read(viewport, "showGridZ", v.showGridZ);
        int viewMode = static_cast<int>(v.viewMode);
        Read(viewport, "viewMode", viewMode, 0, static_cast<int>(Engine::Rendering::ViewMode::ShadowCascades));
        v.viewMode = static_cast<Engine::Rendering::ViewMode>(viewMode);
        Read(viewport, "snapLocation", v.snapLocation);
        Read(viewport, "snapRotation", v.snapRotation);
        Read(viewport, "snapScale", v.snapScale);
        Read(viewport, "locationSnap", v.locationSnap, 0.001f, 10000.0f);
        Read(viewport, "rotationSnap", v.rotationSnap, 0.01f, 360.0f);
        Read(viewport, "scaleSnap", v.scaleSnap, 0.001f, 100.0f);

        const json& panels = Section(data, "panels");
        PanelVisibility& w = p.panels;
        Read(panels, "viewport", w.viewport);
        Read(panels, "worldTree", w.worldTree);
        Read(panels, "properties", w.properties);
        Read(panels, "assetBrowser", w.assetBrowser);
        Read(panels, "console", w.console);
        Read(panels, "profiler", w.profiler);
        Read(panels, "worldSettings", w.worldSettings);
        Read(panels, "projectSettings", w.projectSettings);
        Read(panels, "editorPreferences", w.editorPreferences);
        Read(panels, "plugins", w.plugins);

        return true;
    }

    bool SaveEditorPreferences(const std::string& path, const EditorPreferences& preferences)
    {
        // Written next to the file then moved over it : a crash halfway never leaves a truncated file behind
        const std::string text = SerializeEditorPreferences(preferences);
        const std::string temporary = path + ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if(!file)
            {
                DEBUG_ERROR("Could not write the editor preferences to \"" + temporary + "\"");
                return false;
            }
            file << text;
            if(!file)
            {
                DEBUG_ERROR("Could not write the editor preferences to \"" + temporary + "\"");
                return false;
            }
        }

        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if(error)
        {
            DEBUG_ERROR("Could not save the editor preferences to \"" + path + "\" : " + error.message());
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
    }
}
