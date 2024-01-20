#pragma once

#include <filesystem>

struct Model;

namespace util
{
Model loadModel(const std::filesystem::path& path);
}
