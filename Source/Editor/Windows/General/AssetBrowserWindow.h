#pragma once
// ===========================================================================
// 概要: Assets フォルダを閲覧するウィンドウ。AssetBrowser を所有して描くだけの枠
//
// 責務:
//   - AssetBrowser を生成・所有し、毎フレーム描く
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Editor/Utils/AssetBrowser.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include <memory>

class AssetBrowserWindow : public EditorWindow
{
private:
    std::unique_ptr<AssetBrowser> m_assetBrowser;

public:
    AssetBrowserWindow() : EditorWindow("Asset Browser")
    {
        m_assetBrowser = std::make_unique<AssetBrowser>("./Assets", Graphics::Instance().GetDevice());
    }

protected:
    void DrawContents(EditorContext &context) override
    {
        if (m_assetBrowser)
        {
            m_assetBrowser->Draw();
        }
    }
};
