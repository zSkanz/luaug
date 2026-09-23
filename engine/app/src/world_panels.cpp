#include "luaug/app/world_panels.h"

#if LUAUG_DEBUG_UI

#include "luaug/app/content_tree.h"
#include "luaug/app/editor.h"
#include "luaug/app/inspector.h"
#include "luaug/asset/terrain_palette.h"
#include "luaug/scene/components.h"
#include "luaug/scene/value.h"
#include "luaug/scene/world.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <imgui.h>
#include <string>
#include <string_view>
#include <vector>

namespace luaug::app {

using core::f32;

namespace {

// One gesture per drag of a widget in these panels, opened on the first edit
// and closed when nothing is held -- the Properties panel's rule, so a slider
// dragged across a second is one undo step and not sixty.
struct PanelGesture
{
    core::u64 id = 0;

    void edited(Inspector& inspector)
    {
        if (id == 0)
            id = inspector.beginGesture();
    }
    void settle(Inspector& inspector)
    {
        if (id != 0 && !ImGui::IsAnyItemActive()) {
            if (inspector.gesture() == id)
                inspector.endGesture();
            id = 0;
        }
    }
};

PanelGesture g_settingsGesture;
PanelGesture g_lookGesture;

// A texture slot: "(none)" or one of the project's images, as the URN a block
// type stores. The list is read when the combo OPENS, for the reason the
// property picker reads it then: it is a directory walk.
bool textureSlot(const char* label, ContentTree& content, scene::World& world, core::NameAtom& slot)
{
    static std::vector<std::string> candidates;
    const std::string_view current = slot.valid() ? world.atoms().text(slot) : std::string_view{};
    const std::string preview = current.empty() ? std::string("(none)") : std::string(current);
    bool changed = false;
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushID(label);
    if (ImGui::BeginCombo("##texture", preview.c_str())) {
        if (ImGui::IsWindowAppearing())
            candidates = content.filesOfKind(ContentKind::Texture);
        if (ImGui::Selectable("(none)", current.empty())) {
            slot = core::NameAtom{};
            changed = true;
        }
        for (const std::string& candidate : candidates) {
            const std::string urn = "asset://" + candidate;
            if (ImGui::Selectable(candidate.c_str(), urn == current)) {
                slot = world.atoms().intern(urn);
                changed = true;
            }
        }
        if (candidates.empty())
            ImGui::TextDisabled("No images under content/. Import one first.");
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

} // namespace

void drawTerrainSetup(Editor& editor, scene::World& world, core::InstanceId root, Inspector& inspector,
                      EditorCommands& commands)
{
    const core::InstanceId terrainId = editor.terrainIn(world, root);
    const scene::TerrainComponent* terrain = terrainId.valid() ? world.terrains().find(terrainId) : nullptr;

    // --- Heightmap ------------------------------------------------------------
    if (ImGui::CollapsingHeader("Heightmap")) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("ground from a greyscale image: black is the lowest height, white the highest. "
                            "16-bit PNG and RAW keep smooth slopes; 8-bit shows steps");
        ImGui::PopTextWrapPos();

        const std::filesystem::path& source = editor.heightmapSource();
        const std::string chosen = source.empty() ? std::string("no image chosen") : source.filename().string();
        ImGui::TextUnformatted(chosen.c_str());
        if (!source.empty())
            ImGui::SetItemTooltip("%s", source.string().c_str());
        if (ImGui::Button("Choose Image...", ImVec2(-FLT_MIN, 0.0f)))
            commands.pickHeightmap = true;

        static f32 size = 256.0f;
        static f32 low = 0.0f;
        static f32 high = 64.0f;
        ImGui::TextUnformatted("Size");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragFloat("##heightmap-size", &size, 1.0f, 8.0f, 2048.0f, "%.0f m");
        ImGui::SetItemTooltip("how wide the image is laid, centred on the terrain's position");
        ImGui::TextUnformatted("Black at");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragFloat("##heightmap-low", &low, 0.25f, -1024.0f, 1024.0f, "%.1f m");
        ImGui::TextUnformatted("White at");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragFloat("##heightmap-high", &high, 0.25f, -1024.0f, 1024.0f, "%.1f m");

        // The cost before the click, as the flat-ground form shows it.
        const f32 voxel = terrain != nullptr ? terrain->field.settings().voxelSize : 0.5f;
        const auto columns = static_cast<int>(std::lround(size / std::max(voxel, 0.01f))) + 1;
        ImGui::TextDisabled("%d columns across at %.2f m", columns, static_cast<double>(voxel));
        if (terrain != nullptr) {
            const asset::FieldSettings& settings = terrain->field.settings();
            const auto originY = static_cast<f32>(terrain->origin.y);
            if (std::min(low, high) - originY < settings.minHeight ||
                std::max(low, high) - originY > settings.maxHeight) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("past the terrain's height range; widen it under Settings or it is clamped");
                ImGui::PopTextWrapPos();
            }
        }

        ImGui::BeginDisabled(source.empty());
        if (ImGui::Button(terrain == nullptr ? "Create Terrain from Heightmap" : "Import Heightmap",
                          ImVec2(-FLT_MIN, 0.0f))) {
            (void)editor.importHeightmap(world, root, inspector,
                                         Editor::HeightmapImport{source, size, low, high, editor.brush().material});
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("replaces the ground's height across the square; caves are left as they are, and one "
                              "ctrl-Z takes it back");
    }

    // --- Settings -------------------------------------------------------------
    if (terrain != nullptr && ImGui::CollapsingHeader("Settings")) {
        const asset::FieldSettings& settings = terrain->field.settings();
        const bool empty = terrain->field.empty();
        const auto write = [&](std::string_view property, f32 value) {
            g_settingsGesture.edited(inspector);
            inspector.enqueue(terrainId, world.atoms().intern(property), scene::Value{static_cast<double>(value)});
        };

        ImGui::TextUnformatted("Voxel size");
        ImGui::SetNextItemWidth(-FLT_MIN);
        f32 voxel = settings.voxelSize;
        ImGui::BeginDisabled(!empty);
        if (ImGui::DragFloat("##voxel-size", &voxel, 0.01f, 0.1f, 8.0f, "%.2f m"))
            write("VoxelSize", voxel);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip(empty ? "how coarse the ground is. Smaller resolves more and costs more"
                                    : "only while the terrain is empty: changing it would resample every "
                                      "column. Clear Terrain first");

        ImGui::TextUnformatted("Lowest");
        ImGui::SetNextItemWidth(-FLT_MIN);
        f32 lowest = settings.minHeight;
        if (ImGui::DragFloat("##min-height", &lowest, 0.5f, -4096.0f, settings.maxHeight - 1.0f, "%.1f m"))
            write("MinHeight", lowest);
        ImGui::SetItemTooltip("the deepest anything may dig. Set it before digging: a collider's precision is "
                              "spread over this range");
        ImGui::TextUnformatted("Highest");
        ImGui::SetNextItemWidth(-FLT_MIN);
        f32 highest = settings.maxHeight;
        if (ImGui::DragFloat("##max-height", &highest, 0.5f, settings.minHeight + 1.0f, 4096.0f, "%.1f m"))
            write("MaxHeight", highest);
        ImGui::SetItemTooltip("the highest ground may rise");
        g_settingsGesture.settle(inspector);
    }
}

void drawBlockLook(Editor& editor, scene::World& world, Inspector& inspector)
{
    scene::VoxelComponent* voxels = Editor::voxelsIn(world);
    const asset::BlockId selected = editor.blockType();
    if (voxels == nullptr || selected == asset::AirBlock || selected > voxels->types.size())
        return;
    const scene::VoxelBlockType type = voxels->types[selected - 1u];

    std::array<core::NameAtom, 3> textures{type.texture, type.sideTexture, type.bottomTexture};
    bool changed = textureSlot("Top image", editor.content(), world, textures[0]);
    changed |= textureSlot("Side image", editor.content(), world, textures[1]);
    changed |= textureSlot("Bottom image", editor.content(), world, textures[2]);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("the colours above tint the images, so white shows an image as drawn");
    ImGui::PopTextWrapPos();

    // `Enum.BlockOpacity`, in its order.
    int opacity = std::clamp(type.opacity, 0, 2);
    ImGui::TextUnformatted("Opacity");
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::Combo("##block-opacity", &opacity, "Opaque\0Cutout\0Translucent\0");
    ImGui::SetItemTooltip("Cutout keeps an image's holes, for leaves and fences. Translucent blends, for glass "
                          "and water");
    f32 transparency = type.transparency;
    if (opacity == 2) {
        ImGui::TextUnformatted("Transparency");
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::SliderFloat("##block-transparency", &transparency, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("how much shows through where the image has no alpha of its own");
    }

    if (changed) {
        g_lookGesture.edited(inspector);
        (void)editor.setBlockTypeLook(world, inspector, selected, textures, opacity, transparency, g_lookGesture.id);
    }
    g_lookGesture.settle(inspector);
}

} // namespace luaug::app

#else

namespace luaug::app {

void drawTerrainSetup(Editor&, scene::World&, core::InstanceId, Inspector&, EditorCommands&)
{}
void drawBlockLook(Editor&, scene::World&, Inspector&)
{}

} // namespace luaug::app

#endif
