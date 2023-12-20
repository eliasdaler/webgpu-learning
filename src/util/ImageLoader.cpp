#include "ImageLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

ImageData::~ImageData()
{
    stbi_image_free(pixels);
    stbi_image_free(hdrPixels);
}

namespace util
{
ImageData loadImage(const std::filesystem::path& p)
{
    ImageData data;
    if (stbi_is_hdr(p.string().c_str())) {
        data.hdr = true;
        data.hdrPixels = stbi_loadf(p.string().c_str(), &data.width, &data.height, &data.comp, 0);
    } else {
        data.pixels = stbi_load(p.string().c_str(), &data.width, &data.height, &data.channels, 0);
    }
    return data;
}

} // namespace util
