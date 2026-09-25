// surfacewrap <surface.hlsl> <output directory>
//
// Writes `<variant>.<stage>.hlsl` for every pass variant a surface is drawn
// in. A surface the reflection refuses is a build error, printed by line.
#include "luaug/asset/surface_shader.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

using namespace luaug::asset;

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: surfacewrap <surface.hlsl> <output directory>\n");
        return 2;
    }
    const std::filesystem::path source = std::filesystem::absolute(argv[1]);
    const std::filesystem::path out = argv[2];
    std::ifstream in(source, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "surfacewrap: cannot read %s\n", source.string().c_str());
        return 1;
    }
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    const SurfaceReflection reflection = reflectSurface(text);
    if (!reflection.ok()) {
        for (const SurfaceDiagnostic& error : reflection.errors)
            std::fprintf(stderr, "%s(%u): %s: %s\n", source.string().c_str(), error.line, error.key.c_str(),
                         error.subject.c_str());
        return 1;
    }

    const struct
    {
        SurfaceVariant variant;
        const char* name;
    } variants[] = {
        {SurfaceVariant::Forward, "forward"},
        {SurfaceVariant::ForwardInstanced, "forward_instanced"},
        {SurfaceVariant::ForwardBlended, "forward_blended"},
        {SurfaceVariant::Depth, "depth"},
        {SurfaceVariant::DepthInstanced, "depth_instanced"},
    };
    std::filesystem::create_directories(out);
    const std::string include = source.generic_string();
    for (const auto& entry : variants) {
        for (const auto& [stage, stageName] :
             {std::pair{SurfaceStage::Vertex, "vertex"}, std::pair{SurfaceStage::Fragment, "fragment"}}) {
            const std::filesystem::path file = out / (std::string(entry.name) + "." + stageName + ".hlsl");
            const std::string wrapped = surfaceWrapper(reflection, entry.variant, stage, include);
            // Written only when different, so a build that changed nothing
            // recompiles nothing downstream.
            std::ifstream existing(file, std::ios::binary);
            const std::string before{std::istreambuf_iterator<char>(existing), {}};
            if (before == wrapped)
                continue;
            existing.close();
            std::ofstream write(file, std::ios::binary);
            write << wrapped;
        }
    }
    return 0;
}
