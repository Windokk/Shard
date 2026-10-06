#include "asset_operations.hpp"

#include "engine/platform/process/process.hpp"

namespace Shard::Editor::GUI::AssetOperations
{
    void RevealInFileExplorer(const std::string& nativePath, bool selectItem)
    {
        Engine::Core::Platform::RevealInFileManager(nativePath, selectItem);
    }
}
