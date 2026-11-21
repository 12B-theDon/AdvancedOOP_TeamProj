#pragma once

#include <string>
#include <unordered_map>

class QString;
namespace torch { namespace jit { struct Module; } }

namespace algorithm {
using ArchiveExtras = std::unordered_map<std::string, std::string>;

// Qt opens Unicode paths; LibTorch receives bytes through its reader/writer APIs.
torch::jit::Module LoadArchive(const QString& path, ArchiveExtras& extras);
torch::jit::Module LoadArchive(const QString& path);
void SaveArchive(const torch::jit::Module& module, const QString& path, const ArchiveExtras& extras = {});
}
