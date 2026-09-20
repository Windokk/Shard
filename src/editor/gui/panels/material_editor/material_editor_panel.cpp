#include "material_editor_panel.hpp"

#include "engine/core/engine.hpp"
#include "engine/core/resources/resources_manager.hpp"

#include "engine/rendering/material/material.hpp"
#include "engine/rendering/renderer/renderer.hpp"
#include "engine/rendering/texture/texture.hpp"

#include "engine/debugging/logger.hpp"

#include "imgui/imgui.h"

#include "editor/gui/dragdrop/asset_drag_drop.hpp"
#include "editor/gui/popups.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>

using namespace nlohmann;

namespace Shard::Editor::GUI{

    using namespace Shard::Engine;

    static bool NameLooksLikeColor(const std::string& name)
    {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        return lower.find("color") != std::string::npos || lower.find("colour") != std::string::npos;
    }

    void MaterialEditorPanel::Open(const Filesystem::Path& path)
    {
        this->path = path;
        isOpen = true;
        focusRequested = true;
        Load();
    }

    void MaterialEditorPanel::Load()
    {
        params.clear();
        shader = nullptr;

        if(!path.Exists()){
            DEBUG_ERROR("Material at path \"" + path.full + "\" doesn't exist");
            return;
        }

        std::string src = path.ReadFile();

        json data;
        try{
            data = json::parse(src);
        }
        catch(const json::parse_error& e){
            DEBUG_ERROR("Failed to parse material \"" + path.full + "\": " + e.what());
            return;
        }

        shaderPath = data.value("shader", std::string());
        receivesShadows = data.value("recievesShadows", true);

        std::string mode = data.value("renderMode", std::string("opaque"));
        renderModeIndex = mode == "translucent" ? 2 : (mode == "masked" ? 1 : 0);

        if(!shaderPath.empty())
            shader = Core::GetEngine().GetResourcesManager()->GetShader(shaderPath);

        if(data.contains("uniforms") && data["uniforms"].is_array()){
            for(auto& uniform : data["uniforms"]){
                for(auto it = uniform.begin(); it != uniform.end(); ++it){
                    MatParam param;
                    param.name = it.key();
                    const auto& value = it.value();

                    if(value.is_string()){
                        param.kind = MatParamKind::Texture;
                        param.texturePath = value.get<std::string>();
                    }
                    else if(value.is_boolean()){
                        param.kind = MatParamKind::Bool;
                        param.boolValue = value.get<bool>();
                    }
                    else if(value.is_number_float()){
                        param.kind = MatParamKind::Float;
                        param.floatValue = value.get<float>();
                    }
                    else if(value.is_number_integer()){
                        param.kind = MatParamKind::Int;
                        param.intValue = value.get<int>();
                    }
                    else if(value.is_array()){
                        size_t len = value.size();
                        if(len == 2){
                            param.kind = MatParamKind::Vec2;
                            param.vec2Value = glm::vec2(value[0].get<float>(), value[1].get<float>());
                        }
                        else if(len == 3){
                            param.kind = MatParamKind::Vec3;
                            param.vec3Value = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
                        }
                        else if(len == 4){
                            param.kind = MatParamKind::Vec4;
                            param.vec4Value = glm::vec4(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>());
                        }
                        else if(len == 16){
                            param.kind = MatParamKind::Mat4;
                            for(int i = 0; i < 16; ++i)
                                param.mat4Value[i / 4][i % 4] = value[i].get<float>();
                        }
                        else{
                            DEBUG_ERROR("Unknown array size for uniform: " + param.name);
                            continue;
                        }
                    }
                    else{
                        DEBUG_ERROR("Unsupported uniform value type for: " + param.name);
                        continue;
                    }

                    params.push_back(std::move(param));
                }
            }
        }

        dirty = false;
    }

    void MaterialEditorPanel::Save()
    {
        json out;
        out["shader"] = shaderPath;
        out["recievesShadows"] = receivesShadows;

        static const char* kModes[] = {"opaque", "masked", "translucent"};
        out["renderMode"] = kModes[renderModeIndex];

        json uniformsArr = json::array();
        for(auto& p : params){
            json entry;
            switch(p.kind){
                case MatParamKind::Bool:    entry[p.name] = p.boolValue; break;
                case MatParamKind::Int:     entry[p.name] = p.intValue; break;
                case MatParamKind::Float:   entry[p.name] = p.floatValue; break;
                case MatParamKind::Vec2:    entry[p.name] = {p.vec2Value.x, p.vec2Value.y}; break;
                case MatParamKind::Vec3:    entry[p.name] = {p.vec3Value.x, p.vec3Value.y, p.vec3Value.z}; break;
                case MatParamKind::Vec4:    entry[p.name] = {p.vec4Value.x, p.vec4Value.y, p.vec4Value.z, p.vec4Value.w}; break;
                case MatParamKind::Mat4:{
                    json arr = json::array();
                    for(int c = 0; c < 4; ++c)
                        for(int r = 0; r < 4; ++r)
                            arr.push_back(p.mat4Value[c][r]);
                    entry[p.name] = arr;
                    break;
                }
                case MatParamKind::Texture: entry[p.name] = p.texturePath; break;
            }
            uniformsArr.push_back(entry);
        }
        out["uniforms"] = uniformsArr;

        if(!path.WriteFile(out.dump(4))){
            DEBUG_ERROR("Failed to save material: " + path.full);
            return;
        }

        std::string nameInProject = Core::GetEngine().GetFileManager()->GetFileInfos(path).nameInProject;

        // The edited texture set may differ from what the resident material was loaded with
        Core::GetEngine().GetResourcesManager()->RefreshDependencies(nameInProject);

        // Its asset browser thumbnail shows the old look
        Rendering::ThumbnailRequest thumbnail;
        thumbnail.kind = Rendering::ThumbnailKind::Material;
        thumbnail.nameInProject = nameInProject;
        thumbnail.filePath = path;
        Core::GetEngine().GetRenderer()->GetThumbnailService()->Invalidate(thumbnail);

        dirty = false;
    }

    void MaterialEditorPanel::Draw()
    {
        if(!isOpen)
            return;

        ImGui::SetNextWindowSize(ImVec2(420, 480), ImGuiCond_FirstUseEver);
        if(focusRequested){
            ImGui::SetNextWindowFocus();
            focusRequested = false;
        }

        // Stable ImGui ID (###MaterialEditor) so re-opening a different material re-purposes the
        // same window/dock slot instead of ImGui treating it as a brand-new one because the title changed.
        std::string title = "Material Editor - " + path.GetFilename() + "###MaterialEditor";

        bool wasOpen = isOpen;

        if(ImGui::Begin(title.c_str(), &isOpen)){

            char shaderBuffer[256];
            strncpy(shaderBuffer, shaderPath.c_str(), sizeof(shaderBuffer) - 1);
            shaderBuffer[sizeof(shaderBuffer) - 1] = '\0';

            ImGui::SetNextItemWidth(-FLT_MIN);
            if(ImGui::InputText("##Shader", shaderBuffer, sizeof(shaderBuffer), ImGuiInputTextFlags_EnterReturnsTrue)){
                shaderPath = shaderBuffer;
                shader = Core::GetEngine().GetResourcesManager()->GetShader(shaderPath);
                dirty = true;
            }
            ImGui::SameLine(0, 8);
            ImGui::TextDisabled("Shader");

            static const char* kModes[] = {"Opaque", "Masked", "Translucent"};
            ImGui::SetNextItemWidth(160);
            if(ImGui::Combo("Render Mode", &renderModeIndex, kModes, IM_ARRAYSIZE(kModes)))
                dirty = true;

            if(ImGui::Checkbox("Receives Shadows", &receivesShadows))
                dirty = true;

            ImGui::TextDisabled("Shader/render mode changes apply after the level is reloaded.");

            ImGui::Separator();
            ImGui::Text("Parameters");
            ImGui::SameLine(ImGui::GetWindowWidth() - 118);
            if(ImGui::Button("+ Add Parameter"))
                ImGui::OpenPopup("AddMaterialParameter");

            DrawAddParameterPopup();

            int removeIndex = -1;

            if(ImGui::BeginTable("##MaterialParams", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)){
                ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthStretch, 80.0f);
                ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch, 110.0f);
                ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed, 24.0f);

                for(int i = 0; i < (int)params.size(); ++i){
                    ImGui::PushID(i);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(params[i].name.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if(DrawParam(params[i])){
                        dirty = true;
                        ApplyParamLive(params[i]);
                    }

                    ImGui::TableSetColumnIndex(2);
                    if(ImGui::SmallButton("x"))
                        removeIndex = i;

                    ImGui::PopID();
                }

                ImGui::EndTable();
            }

            if(removeIndex != -1){
                params.erase(params.begin() + removeIndex);
                dirty = true;
            }

            ImGui::Separator();

            ImGui::BeginDisabled(!dirty);
            if(ImGui::Button("Save"))
                Save();
            ImGui::EndDisabled();

            ImGui::SameLine();
            if(ImGui::Button("Revert"))
                Load();

            if(dirty){
                ImGui::SameLine();
                ImGui::TextDisabled("(unsaved changes)");
            }
        }
        ImGui::End();

        // The X button flips isOpen to false via the pointer passed to Begin() above - catch that
        // transition here (rather than bailing out early next frame) so a dirty material can't be
        // silently unloaded: keep the window open and ask the user what to do first.
        if(wasOpen && !isOpen && dirty){
            isOpen = true;

            Popups::ConfirmUnsavedChanges(path.GetFilename(),
                [this](){ Save(); isOpen = false; },
                [this](){ dirty = false; isOpen = false; });
        }
    }

    bool MaterialEditorPanel::DrawParam(MatParam& param)
    {
        bool changed = false;

        switch(param.kind){
            case MatParamKind::Bool:
                changed = ImGui::Checkbox("##v", &param.boolValue);
                break;
            case MatParamKind::Int:
                changed = ImGui::DragInt("##v", &param.intValue);
                break;
            case MatParamKind::Float:
                changed = ImGui::DragFloat("##v", &param.floatValue, 0.05f);
                break;
            case MatParamKind::Vec2:
                changed = ImGui::DragFloat2("##v", &param.vec2Value.x, 0.05f);
                break;
            case MatParamKind::Vec3:
                changed = NameLooksLikeColor(param.name)
                    ? ImGui::ColorEdit3("##v", &param.vec3Value.x)
                    : ImGui::DragFloat3("##v", &param.vec3Value.x, 0.05f);
                break;
            case MatParamKind::Vec4:
                changed = NameLooksLikeColor(param.name)
                    ? ImGui::ColorEdit4("##v", &param.vec4Value.x)
                    : ImGui::DragFloat4("##v", &param.vec4Value.x, 0.05f);
                break;
            case MatParamKind::Mat4:
                ImGui::TextDisabled("mat4 (not editable here)");
                break;
            case MatParamKind::Texture:{
                char buffer[256];
                strncpy(buffer, param.texturePath.c_str(), sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = '\0';
                if(ImGui::InputText("##v", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue)){
                    param.texturePath = buffer;
                    changed = true;
                }

                if(ImGui::BeginDragDropTarget()){
                    std::vector<std::string> dropped = DragDrop::AcceptAssetDragDropPayload();
                    if(!dropped.empty()){
                        param.texturePath = dropped[0];
                        changed = true;
                    }
                    ImGui::EndDragDropTarget();
                }
                break;
            }
        }

        return changed;
    }

    void MaterialEditorPanel::DrawAddParameterPopup()
    {
        if(!ImGui::BeginPopup("AddMaterialParameter"))
            return;

        if(!shader){
            ImGui::TextDisabled("No shader loaded");
            ImGui::EndPopup();
            return;
        }

        auto alreadyUsed = [&](const std::string& name){
            for(auto& p : params)
                if(p.name == name)
                    return true;
            return false;
        };

        bool any = false;

        for(auto& [name, info] : shader->GetActiveUniformsMap()){
            if(alreadyUsed(name))
                continue;

            MatParamKind kind;
            switch(info.type){
                case Rendering::ShaderDataType::Bool:  kind = MatParamKind::Bool; break;
                case Rendering::ShaderDataType::Int:   kind = MatParamKind::Int; break;
                case Rendering::ShaderDataType::Float: kind = MatParamKind::Float; break;
                case Rendering::ShaderDataType::Vec2:  kind = MatParamKind::Vec2; break;
                case Rendering::ShaderDataType::Vec3:  kind = MatParamKind::Vec3; break;
                case Rendering::ShaderDataType::Vec4:  kind = MatParamKind::Vec4; break;
                case Rendering::ShaderDataType::Mat4:  kind = MatParamKind::Mat4; break;
                default: continue; // Mat2/Mat3/None aren't editable here
            }

            any = true;
            if(ImGui::MenuItem(name.c_str())){
                MatParam param;
                param.name = name;
                param.kind = kind;
                params.push_back(param);
                dirty = true;
                ApplyParamLive(param);
                ImGui::CloseCurrentPopup();
            }
        }

        for(auto& [name, samplerInfo] : shader->GetActiveSamplersMap()){
            if(alreadyUsed(name))
                continue;

            any = true;
            if(ImGui::MenuItem((name + " (texture)").c_str())){
                MatParam param;
                param.name = name;
                param.kind = MatParamKind::Texture;
                params.push_back(param);
                dirty = true;
                ImGui::CloseCurrentPopup();
            }
        }

        if(!any)
            ImGui::TextDisabled("All shader parameters already added");

        ImGui::EndPopup();
    }

    void MaterialEditorPanel::ApplyParamLive(const MatParam& param)
    {
        auto& engine = Core::GetEngine();
        std::string pathInProject = engine.GetFileManager()->GetFileInfos(path).nameInProject;
        auto mat = engine.GetResourcesManager()->GetMaterial(pathInProject);

        if(!mat)
            return;

        switch(param.kind){
            case MatParamKind::Bool:  mat->SetScalarParameter(param.name, param.boolValue); break;
            case MatParamKind::Int:   mat->SetScalarParameter(param.name, param.intValue); break;
            case MatParamKind::Float: mat->SetScalarParameter(param.name, param.floatValue); break;
            case MatParamKind::Vec2:  mat->SetScalarParameter(param.name, param.vec2Value); break;
            case MatParamKind::Vec3:  mat->SetScalarParameter(param.name, param.vec3Value); break;
            case MatParamKind::Vec4:  mat->SetScalarParameter(param.name, param.vec4Value); break;
            case MatParamKind::Mat4:  mat->SetScalarParameter(param.name, param.mat4Value); break;
            case MatParamKind::Texture:{
                auto tex = engine.GetResourcesManager()->GetTexture(param.texturePath);
                if(tex)
                    mat->SetTextureParameter(param.name, tex->GetHandle());
                break;
            }
        }
    }
}
