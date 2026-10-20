#include "world_settings_panel.hpp"

#include "engine/world/engine.hpp"
#include "engine/assets/vfs/filesystem.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/renderer/components/render_world_data.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/features/lighting/lightmap_manager.hpp"

#include "apps/editor/gui/dragdrop/asset_drag_drop.hpp"
#include "apps/editor/gui/IconsLucide.h"
#include "apps/editor/gui/tag_widgets.hpp"

#include "imgui/imgui.h"

#include <cstring>

using namespace Shard::Engine;
using Shard::Engine::Core::GetEngine;

namespace Shard::Editor::GUI{

    void WorldSettingsPanel::Draw()
    {
        if (!ImGui::Begin("World Settings"))
        {
            ImGui::End();
            return;
        }

        Worlds::World* world = GetEngine().GetWorldManager()->GetWorldAt(0);
        if (!world)
        {
            ImGui::TextDisabled("No world loaded");
            ImGui::End();
            return;
        }

        DrawGeneralCategory();
        DrawRenderingCategory();

        ImGui::End();
    }

    void WorldSettingsPanel::DrawGeneralCategory()
    {
        Worlds::World* world = GetEngine().GetWorldManager()->GetWorldAt(0);

        if (!ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::BeginDisabled();

        char nameBuffer[256];
        strncpy(nameBuffer, world->GetName().c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer));

        char pathBuffer[512];
        strncpy(pathBuffer, world->GetPath().full.c_str(), sizeof(pathBuffer) - 1);
        pathBuffer[sizeof(pathBuffer) - 1] = '\0';
        ImGui::InputText("Path", pathBuffer, sizeof(pathBuffer));

        ImGui::EndDisabled();

        // The world's own tags (a game mode, a biome...) : a script reads them with GetTags() on its world
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Tags");
        ImGui::SameLine();
        Core::TagContainer tags = world->GetTags();
        if (TagWidgets::TagContainerEditor("##WorldTags", tags))
        {
            world->GetTags() = tags;
            world->SetDirty(true);
        }
    }

    void WorldSettingsPanel::ApplySkybox(Worlds::World* world, const std::string& pathInProject)
    {
        m_SkyboxError.clear();

        if (pathInProject.empty())
        {
            world->Ext<Rendering::RenderWorldData>().ClearSkybox();
            world->SetDirty(true);
            return;
        }

        auto* assets = Core::GetEngine().GetAssetIDManager();
        std::shared_ptr<Filesystem::AssetInfos> info = assets->GetAssetFromID(assets->GetIDFromNameInProject(pathInProject));

        if (!info || info->baseInfos.nameInProject != pathInProject)
        {
            m_SkyboxError = "Unknown asset : " + pathInProject;
            return;
        }

        if (info->baseInfos.type != Filesystem::Type::T_IMAGE)
        {
            m_SkyboxError = pathInProject + " isn't an image.";
            return;
        }

        if (pathInProject == world->Ext<Rendering::RenderWorldData>().GetSkyboxPath())
            return;

        if (!world->Ext<Rendering::RenderWorldData>().SetSkybox(pathInProject))
        {
            m_SkyboxError = "Couldn't use " + pathInProject + " as a skybox : it must be an equirectangular RGB image (see the console).";
            return;
        }

        world->SetDirty(true);
    }

    void WorldSettingsPanel::DrawSkyboxSection(Worlds::World* world)
    {
        if (!ImGui::TreeNodeEx("Skybox", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
            return;

        // A different world is now shown - drop the previous one's half-typed text and error.
        if (world != m_LastWorld)
        {
            m_LastWorld = world;
            m_SkyboxInputActive = false;
            m_SkyboxError.clear();
        }

        if (!m_SkyboxInputActive)
            m_SkyboxInput = world->Ext<Rendering::RenderWorldData>().GetSkyboxPath();

        char buffer[256];
        strncpy(buffer, m_SkyboxInput.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        const float clearWidth = ImGui::GetFrameHeight();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Skybox file");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(-(clearWidth + ImGui::GetStyle().ItemSpacing.x));
        const bool submitted = ImGui::InputTextWithHint("##SkyboxFile", "None - drop an image here", buffer, sizeof(buffer),
                                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        m_SkyboxInputActive = ImGui::IsItemActive();
        if (m_SkyboxInputActive)
            m_SkyboxInput = buffer;

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Equirectangular image used as the world's sky and image-based lighting.\nDrag one from the asset browser, or type its path and press Enter.");

        if (submitted)
            ApplySkybox(world, buffer);

        // Same drag-and-drop the Properties panel's asset fields accept.
        if (ImGui::BeginDragDropTarget())
        {
            std::vector<std::string> dropped = DragDrop::AcceptAssetDragDropPayload();
            if (!dropped.empty())
                ApplySkybox(world, dropped[0]);
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(world->Ext<Rendering::RenderWorldData>().GetSkyboxPath().empty());
        if (ImGui::Button((std::string(ICON_LC_X) + "##ClearSkybox").c_str(), ImVec2(clearWidth, clearWidth)))
            ApplySkybox(world, "");
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Remove the skybox");

        if (!m_SkyboxError.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.40f, 1.0f));
            ImGui::TextWrapped("%s", m_SkyboxError.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::TreePop();
    }

    void WorldSettingsPanel::DrawDisplaySection(Worlds::World* world)
    {
        if (!ImGui::TreeNodeEx("Display", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
            return;

        Rendering::DisplaySettings& display = world->Ext<Rendering::RenderWorldData>().display;
        bool changed = false;

        struct Entry { Rendering::Tonemapper tonemapper; const char* label; const char* hint; };
        static const Entry kEntries[] = {
            { Rendering::Tonemapper::ACES, "ACES (filmic)", "The Academy filmic curve : contrasty, bright colours roll off toward white. The look of most games." },
            { Rendering::Tonemapper::Neutral, "Neutral", "Khronos PBR Neutral : leaves what is below ~0.8 as it is and only compresses highlights, so a material keeps its colour." },
            { Rendering::Tonemapper::Reinhard, "Reinhard", "A simple soft shoulder on the luminance." },
            { Rendering::Tonemapper::None, "None (clamp)", "No curve : everything brighter than white is white. Exposure and the sRGB curve still apply." },
        };

        int current = 0;
        for (int i = 0; i < 4; i++)
            if (kEntries[i].tonemapper == display.tonemapper)
                current = i;

        if (ImGui::BeginCombo("Tonemapper", kEntries[current].label))
        {
            for (int i = 0; i < 4; i++)
            {
                if (ImGui::Selectable(kEntries[i].label, i == current))
                {
                    display.tonemapper = kEntries[i].tonemapper;
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", kEntries[i].hint);
            }
            ImGui::EndCombo();
        }

        changed |= ImGui::SliderFloat("Exposure (EV)", &display.exposure, -6.0f, 6.0f, "%+.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Stops of light added before the curve : +1 doubles the brightness of the scene, -1 halves it.\nCtrl+click to type a value.");

        ImGui::SameLine();
        if (ImGui::Button("Reset##Exposure"))
        {
            display.exposure = 0.0f;
            changed = true;
        }

        if (changed)
            world->SetDirty(true);

        ImGui::TextDisabled("The scene is lit in linear HDR and mapped to the screen by this, then sRGB encoded.");

        ImGui::TreePop();
    }

    void WorldSettingsPanel::DrawReflectionsSection(Worlds::World* world)
    {
        if (!ImGui::TreeNodeEx("Screen-Space Reflections", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
            return;

        Rendering::SSRSettings& ssr = world->Ext<Rendering::RenderWorldData>().ssr;
        bool changed = false;

        changed |= ImGui::Checkbox("Enabled", &ssr.enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("What the screen shows is reflected by the shiny surfaces, in place of the probe's / the sky's reflection.\nOnly what is on the screen can be reflected : the rest falls back to the probes and the sky.");

        if (!ssr.enabled)
            ImGui::BeginDisabled();

        // ---- Quality : the cost knobs, as a preset or by hand ----
        struct QualityEntry { Rendering::SSRQuality quality; const char* label; };
        static const QualityEntry kQualities[] = {
            { Rendering::SSRQuality::Low, "Low" },
            { Rendering::SSRQuality::Medium, "Medium" },
            { Rendering::SSRQuality::High, "High" },
            { Rendering::SSRQuality::Ultra, "Ultra" },
            { Rendering::SSRQuality::Custom, "Custom" },
        };

        int currentQuality = 4;
        for (int i = 0; i < 5; i++)
            if (kQualities[i].quality == ssr.quality)
                currentQuality = i;

        if (ImGui::BeginCombo("Quality", kQualities[currentQuality].label))
        {
            for (int i = 0; i < 5; i++)
            {
                if (ImGui::Selectable(kQualities[i].label, i == currentQuality))
                {
                    ssr.ApplyQuality(kQualities[i].quality);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A preset of the cost knobs below (steps, resolution, temporal).\nLow : a quarter of the screen. Ultra : the whole screen, 96 steps.");

        // A cost knob touched by hand makes the quality Custom
        bool costChanged = false;
        costChanged |= ImGui::SliderInt("Max steps", &ssr.maxSteps, 8, 128);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Iterations of the depth march, at most. A ray that has not found anything by then is a miss.");

        static const int kDivisors[] = { 1, 2, 4 };
        static const char* kDivisorLabels[] = { "Full", "Half", "Quarter" };
        int divisorIndex = ssr.resolutionDivisor >= 4 ? 2 : (ssr.resolutionDivisor >= 2 ? 1 : 0);
        if (ImGui::BeginCombo("Resolution", kDivisorLabels[divisorIndex]))
        {
            for (int i = 0; i < 3; i++)
            {
                if (ImGui::Selectable(kDivisorLabels[i], i == divisorIndex))
                {
                    ssr.resolutionDivisor = kDivisors[i];
                    costChanged = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The size of the trace relative to the screen (1, 1/2, 1/4 of each side). The result is upsampled, aware of the edges.");

        costChanged |= ImGui::Checkbox("Temporal", &ssr.temporal);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Blends the hit of a pixel with the one of the frame before : steadier edges, especially on a small trace.");

        if (!ssr.temporal)
            ImGui::BeginDisabled();
        changed |= ImGui::SliderFloat("Temporal blend", &ssr.temporalBlend, 0.0f, 0.98f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How much of the previous frame stays. It is lowered by itself while the camera moves.");
        if (!ssr.temporal)
            ImGui::EndDisabled();

        if (costChanged)
        {
            ssr.quality = Rendering::SSRQuality::Custom;
            changed = true;
        }

        ImGui::Separator();

        // ---- Look ----
        changed |= ImGui::SliderFloat("Intensity", &ssr.intensity, 0.0f, 1.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How much of the screen-space reflection replaces the probe / sky reflection where a ray found something.");

        changed |= ImGui::DragFloat("Max distance", &ssr.maxDistance, 0.5f, 1.0f, 500.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How far a reflected ray travels, in world units.");

        changed |= ImGui::DragFloat("Thickness", &ssr.thickness, 0.005f, 0.01f, 5.0f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How far behind a surface of the depth buffer a ray may pass and still hit it : the buffer has no back of things.\nToo small : rays slip behind thin objects. Too big : objects smear into the reflections behind them.");

        changed |= ImGui::SliderFloat("Max roughness", &ssr.maxRoughness, 0.05f, 1.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Beyond this roughness a surface is left to the probes and the sky. The reflection fades out from 60%% of it.");

        changed |= ImGui::SliderFloat("Roughness blur", &ssr.roughnessBlur, 0.0f, 4.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Scales how much a rough surface blurs what it reflects (1 : the width of its specular lobe).");

        changed |= ImGui::SliderFloat("Edge fade", &ssr.edgeFade, 0.0f, 0.5f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Fraction of the screen, from its edges, over which a reflection fades out : what it reflected is leaving the screen.");

        if (ImGui::Button("Reset##SSR"))
        {
            const bool wasEnabled = ssr.enabled;
            ssr = Rendering::SSRSettings{};
            ssr.enabled = wasEnabled;
            changed = true;
        }

        if (!ssr.enabled)
            ImGui::EndDisabled();

        if (changed)
            world->SetDirty(true);

        ImGui::TextDisabled("Traced against the depth of the view, lit from the previous frame's picture.");

        ImGui::TreePop();
    }

    void WorldSettingsPanel::DrawLightmapsSection(Worlds::World* world)
    {
        if (!ImGui::TreeNodeEx("Lightmaps", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
            return;

        Rendering::RenderWorldData& data = world->Ext<Rendering::RenderWorldData>();
        Rendering::LightmapSettings& settings = data.lightmapSettings;
        auto lightmaps = GetEngine().GetRenderer() ? GetEngine().GetRenderer()->GetLightmapManager() : nullptr;

        const bool baking = lightmaps && lightmaps->IsBaking();
        const bool baked = !data.lightmapData.empty();

        if (baking)
            ImGui::TextDisabled("Baking - %s (%d%%)", lightmaps->GetBakePhase(), int(lightmaps->GetBakeProgress() * 100.0f + 0.5f));
        else if (baked)
            ImGui::TextDisabled("Baked : %s", data.lightmapData.c_str());
        else
            ImGui::TextDisabled("Not baked - the models are lit by the probes and the sky.");

        bool changed = false;

        ImGui::BeginDisabled(baking);

        changed |= ImGui::DragFloat("Texels per meter", &settings.texelsPerMeter, 0.5f, 1.0f, 512.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How sharp the baked light is : the lightmap texels one meter of surface gets, on every model.\n8 is soft light on large surfaces, 32 is a crisp shadow edge, 64+ is for hero props.");

        int samples = int(settings.samples);
        if (ImGui::DragInt("Samples", &samples, 1.0f, 1, 16384))
        {
            settings.samples = uint32_t(std::max(samples, 1));
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light paths traced from every texel. The noise falls with the square root of this ;\nthe denoiser removes most of what is left. 256 is a clean result, 1024+ a final one.");

        int bounces = int(settings.maxBounces);
        if (ImGui::DragInt("Bounces", &bounces, 0.1f, 1, 16))
        {
            settings.maxBounces = uint32_t(std::max(bounces, 1));
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How many times the light can bounce before it is dropped : 1 is direct light only (and sky),\neach more adds the light the walls throw on each other (colour bleeding).");

        changed |= ImGui::DragFloat("Indirect intensity", &settings.indirectIntensity, 0.01f, 0.0f, 16.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A multiplier on the light bounced between surfaces and brought in by the sky (not on a Static light's own\ndirect light). 1 is physically correct ; 2-4 fills the shadows for a brighter, flatter look.");

        changed |= ImGui::Checkbox("Denoise", &settings.denoise);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Filters the noise of the bake with an edge aware filter that follows the surfaces and the shadows.");

        // The denoiser's own settings : a filter run once, at the end of the bake of every page
        if (ImGui::TreeNode("Denoiser settings"))
        {
            ImGui::BeginDisabled(!settings.denoise);

            int passes = int(settings.denoiseIterations);
            if (ImGui::SliderInt("Passes", &passes, 1, 8))
            {
                settings.denoiseIterations = uint32_t(std::max(passes, 1));
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Filter passes. Each one doubles the reach (taps 1, 2, 4... texels apart), so more passes smooth wider blotches\nof noise, and cost a little more. 5 is the default.");

            changed |= ImGui::SliderFloat("Light tolerance", &settings.denoiseLuminanceSigma, 0.5f, 16.0f, "%.1f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How many standard deviations of noise a difference of light may be and still be averaged away.\nHigher : smoother, but soft shadow edges and small bright details blur. Lower : sharper, noisier. Default 4.");

            changed |= ImGui::SliderFloat("Normal sharpness", &settings.denoiseNormalPower, 1.0f, 256.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How fast a neighbour on a surface facing another way stops counting (exponent of the dot product of the normals).\nHigher keeps corners and creases crisp ; lower lets light mix across them. Default 48.");

            changed |= ImGui::SliderFloat("Plane tolerance", &settings.denoisePlaneSigma, 0.25f, 8.0f, "%.2f texels");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How far, in texels, a neighbour may stand from the surface's plane and still count. Default 2.");

            if (ImGui::Button("Reset denoiser"))
            {
                const Rendering::LightmapSettings defaults;
                settings.denoiseIterations = defaults.denoiseIterations;
                settings.denoiseLuminanceSigma = defaults.denoiseLuminanceSigma;
                settings.denoiseNormalPower = defaults.denoiseNormalPower;
                settings.denoisePlaneSigma = defaults.denoisePlaneSigma;
                changed = true;
            }

            ImGui::EndDisabled();
            ImGui::TreePop();
        }

        ImGui::SetNextItemWidth(120.0f);
        changed |= ImGui::DragFloat("Firefly clamp", &settings.fireflyClamp, 0.5f, 0.0f, 1000.0f, settings.fireflyClamp > 0.0f ? "%.1f" : "off");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Limits the brightness one path may bring back, so a rare very bright one doesn't leave a white speck. 0 = off.");

        static const int kPageSizes[] = { 512, 1024, 2048, 4096, 8192 };
        int pageIndex = 2;
        for (int i = 0; i < 5; i++)
            if (int(settings.maxPageSize) == kPageSizes[i])
                pageIndex = i;
        const char* pageLabels[] = { "512", "1024", "2048", "4096", "8192" };
        if (ImGui::Combo("Max page size", &pageIndex, pageLabels, 5))
        {
            settings.maxPageSize = uint32_t(kPageSizes[pageIndex]);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The lightmaps of the models are packed into pages (textures) of at most this size, 4 bytes a texel.");

        int maxResolution = int(settings.maxResolution);
        if (ImGui::DragInt("Max resolution per model", &maxResolution, 8.0f, 16, 4096))
        {
            settings.maxResolution = uint32_t(std::max(maxResolution, 16));
            changed = true;
        }

        ImGui::EndDisabled();

        if (changed)
            world->SetDirty(true);

        ImGui::Spacing();

        if (baking)
        {
            ImGui::ProgressBar(lightmaps->GetBakeProgress(), ImVec2(-1.0f, 0.0f));
            if (ImGui::Button(ICON_LC_X " Cancel##LightmapBake"))
                lightmaps->CancelBake();
        }
        else
        {
            ImGui::BeginDisabled(!lightmaps);
            if (ImGui::Button((std::string(baked ? ICON_LC_REFRESH_CW " Re-bake" : ICON_LC_SUN " Bake") + "##LightmapBake").c_str()))
                data.BakeLightmaps(*world);
            ImGui::EndDisabled();

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Bakes the diffuse light of the sky, the Static lights and everything they bounce off into lightmaps\nfor the models marked Static, and saves them next to the world. Save the world afterwards to keep the result.");

            if (baked)
            {
                ImGui::SameLine();
                if (ImGui::Button(ICON_LC_TRASH_2 " Delete##LightmapBake"))
                    data.ClearLightmaps(*world);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Deletes the baked lightmaps : the models are lit by the probes again.");
            }
        }

        ImGui::TextDisabled("Static light : set a Light's Bake Mode to Static (baked) or Stationary (indirect light baked).");
        ImGui::TextDisabled("Moving models : untick Static Lighting in their Model component.");

        ImGui::TreePop();
    }

    void WorldSettingsPanel::DrawRenderingCategory()
    {
        Worlds::World* world = GetEngine().GetWorldManager()->GetWorldAt(0);

        if (!ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::Indent();

        DrawSkyboxSection(world);

        // Ambient - see the comment on World::ambientColor/lit.frag's SampleSSAO for the exact
        // semantics : an additive light floor always present, even with zero real lights/DDGI/IBL.
        if (ImGui::TreeNodeEx("Ambient", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
        {
            if (ImGui::ColorEdit3("Color", &world->ambientColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float))
                world->SetDirty(true);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Screen-Space Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
        {
            if (ImGui::Checkbox("Enabled", &world->ssaoEnabled))
                world->SetDirty(true);

            if (!world->ssaoEnabled)
                ImGui::BeginDisabled();

            bool changed = false;
            changed |= ImGui::DragFloat("Radius", &world->ssaoRadius, 0.01f, 0.01f, 5.0f);
            changed |= ImGui::DragFloat("Bias", &world->ssaoBias, 0.001f, 0.0f, 0.5f);
            changed |= ImGui::DragFloat("Power", &world->ssaoPower, 0.05f, 0.5f, 6.0f);
            changed |= ImGui::DragFloat("Intensity", &world->ssaoIntensity, 0.01f, 0.0f, 3.0f);

            if (changed)
                world->SetDirty(true);

            if (!world->ssaoEnabled)
                ImGui::EndDisabled();

            ImGui::TreePop();
        }

        DrawReflectionsSection(world);

        DrawDisplaySection(world);

        DrawLightmapsSection(world);

        ImGui::Unindent();
    }
}
