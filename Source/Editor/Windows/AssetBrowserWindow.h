#pragma once
#include "Editor/Core/EditorWindow.h"

class AssetBrowserWindow : public EditorWindow {
public:
    AssetBrowserWindow() : EditorWindow("Asset Browser") {}


    //assetBrowser = std::make_unique<AssetBrowser>("./Data", Graphics::Instance().GetDevice());

protected:
    void DrawContents(EditorContext& context) override {
        if (context.assetBrowser) {
            context.assetBrowser->Draw(); // 既存のDrawを呼び出すだけ
        }
    }
};