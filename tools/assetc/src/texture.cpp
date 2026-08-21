#include "luaug/asset/image.h"
#include "luaug/assetc/compiler.h"
#include "luaug/core/i18n.h"
#include "luaug/core/text_key.h"

#include <basisu_comp.h>
#include <basisu_enc.h>
#include <cstring>
#include <mutex>

namespace luaug::assetc {
namespace {

using core::I18nArg;

void ensureEncoderReady()
{
    static std::once_flag once;
    // OpenCL off, and the parameter is why the call is written out rather than
    // defaulted: an asset build that depended on a GPU driver being installed
    // would produce different bytes on a machine without one.
    std::call_once(once, [] { (void)basisu::basisu_encoder_init(false, false); });
}

} // namespace

std::optional<core::EngineError> encodeTexture(const asset::Image& image, std::vector<std::byte>& out)
{
    out.clear();

    if (!image.valid()) {
        const I18nArg args[] = {{"code", "no pixels"}};
        return core::makeError(LUAUG_TR("asset.texture.err.encode_failed"), args);
    }
    ensureEncoderReady();

    basisu::image source(image.width, image.height);
    std::memcpy(reinterpret_cast<void*>(source.get_ptr()), image.pixels.data(), image.pixels.size());

    basisu::basis_compressor_params params;
    params.m_source_images.push_back(source);

    // **UASTC rather than ETC1S**, and standardized UASTC rather than XUASTC.
    // `docs/research/ecosystem-2026.md:193` records that XUASTC was still not
    // standardized in early 2026, and a texture format a future transcoder
    // might not read is the last thing an asset pipeline should reach for.
    // UASTC costs bits and buys quality; the pack is on disk, not in RAM.
    params.m_uastc = true;
    params.m_pack_uastc_ldr_4x4_flags = basisu::cPackUASTCLevelDefault;
    params.m_rdo_uastc_ldr_4x4 = false;

    // Zstd supercompression inside the KTX2 container. It is what makes UASTC
    // affordable on disk, and the runtime transcoder is built with the matching
    // support (`BASISD_SUPPORT_KTX2_ZSTD=1`).
    params.m_create_ktx2_file = true;
    params.m_ktx2_uastc_supercompression = basist::KTX2_SS_ZSTANDARD;

    params.m_mip_gen = true;
    // One call rather than three fields: upstream renamed the transfer-function
    // parameter in 2026 and documents `set_srgb_options` as the way to keep the
    // three consistent (`basisu_comp.h:599-606`). Three fields set by hand is
    // three chances to leave one behind at the next pin bump.
    params.set_srgb_options(true);

    // Nothing is written by the encoder; the bytes come back in memory and this
    // tool decides where they go. An encoder that wrote files of its own would
    // put half a build on disk after a failure.
    params.m_write_output_basis_or_ktx2_files = false;
    params.m_read_source_images = false;
    params.m_status_output = false;

    // **Single-threaded, and that is a determinism decision** (M7 brief,
    // Decision 1) rather than a simplification: an encoder that partitions work
    // across threads may resolve ties differently depending on completion
    // order, and a texture whose bytes depend on how busy the machine was is a
    // content hash that is not a name.
    //
    // The pool is still required -- `basis_compressor::init` refuses a null one
    // outright -- and `job_pool(1)` is the serial configuration: upstream counts
    // the CALLING thread in that number (`basisu_enc.h:827`), so one means no
    // extra threads at all.
    basisu::job_pool pool(1);
    params.m_pJob_pool = &pool;
    params.m_multithreading = false;

    basisu::basis_compressor compressor;
    if (!compressor.init(params)) {
        const I18nArg initArgs[] = {{"code", "init"}};
        return core::makeError(LUAUG_TR("asset.texture.err.encode_failed"), initArgs);
    }

    const basisu::basis_compressor::error_code code = compressor.process();
    if (code != basisu::basis_compressor::cECSuccess) {
        const I18nArg args[] = {{"code", std::to_string(static_cast<int>(code))}};
        return core::makeError(LUAUG_TR("asset.texture.err.encode_failed"), args);
    }

    const basisu::uint8_vec& ktx2 = compressor.get_output_ktx2_file();
    out.resize(ktx2.size());
    if (!ktx2.empty()) {
        std::memcpy(out.data(), ktx2.data(), ktx2.size());
    }
    return std::nullopt;
}

} // namespace luaug::assetc
