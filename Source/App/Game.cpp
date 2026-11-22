#include "Game.h"

#include "Logger.h"
#include "SceneFactory.h"
#include "SceneId.h"
#include "TextureManager.h"
#include <shellapi.h>
#include <algorithm>
#include <utility>

bool Game::Initialize()
{
    int argumentCount = 0;
    std::unique_ptr<LPWSTR, decltype(&LocalFree)> arguments
    {
        CommandLineToArgvW(GetCommandLineW(), &argumentCount), &LocalFree
    };
    if (!arguments) { return false; }

    std::vector<std::wstring> sceneArguments(arguments.get() + 1, arguments.get() + argumentCount);
    const bool smokeTest = std::ranges::find(sceneArguments, L"--smoke-test") != sceneArguments.end();
    if (!FPFramework::Initialize()) { return false; }

#ifdef USE_IMGUI
    if (smokeTest) {
        const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        if (FAILED(directX_.GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&smokeHeap_)))) {
            Logger::Log(log_, "Failed to create the smoke test descriptor heap.");
            return false;
        }
    }
#endif
    return StartScenes(std::make_unique<SceneFactory>(), SceneId::Title, smokeTest, std::move(sceneArguments));
}

void Game::Draw()
{
    FPFramework::Draw();
#ifdef USE_IMGUI
    if (smokeHeap_ && sceneManager_.GetSceneFrame() == 1) {
        ID3D12DescriptorHeap* heaps[] = {smokeHeap_.Get()};
        directX_.GetCommandList()->SetDescriptorHeaps(1, heaps);
    }
#endif
}

void Game::OnFrameComplete()
{
    if (!IsSceneSmokeTest()) { return; }
    if (textures_->GetPendingUploadCount() != 0) {
        Logger::Log(log_, "Texture upload resources were not released after GPU completion.");
        RequestEnd(-1);
        return;
    }
    switch (smokePhase_) {
    case SmokePhase::Title:
        if (!sceneManager_.ChangeScene(SceneId::Gameplay)) {
            RequestEnd(1);
            break;
        }
        smokePhase_ = SmokePhase::Gameplay;
        break;
    case SmokePhase::Gameplay:
        if (sceneManager_.GetSceneFrame() != 3) { break; }
        Logger::Log(log_, "Smoke test completed: 3 frames rendered.");
        if (!sceneManager_.DidSmokeTestPass() || !sceneManager_.HasPendingScene()) {
            RequestEnd(1);
            break;
        }
        smokePhase_ = SmokePhase::ReturnToTitle;
        break;
    case SmokePhase::ReturnToTitle: {
        const bool returned = sceneManager_.GetSceneFrame() == 1 && !sceneManager_.HasPendingScene();
        Logger::Log(log_, returned ? "Scene smoke: title -> gameplay -> title completed."
            : "Scene smoke: return to title did not complete.");
        RequestEnd(returned ? 0 : 1);
        break;
    }
    }
}
