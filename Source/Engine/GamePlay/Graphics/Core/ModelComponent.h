#pragma once
#include <memory>
#include "Engine/Graphics/Resource/Model.h" // Modelクラスはソース内に存在します [5]
#include "Engine/Graphics/Core/Graphics.h"

// ECSデータとして使用されるModelComponent
// 注: これはソースには含まれていない仮の定義です。
struct ModelComponent
{
	std::shared_ptr<Model> modelPtr;
	std::string assetPath;

	ModelComponent() = default;

    // パターンA: パスから生成（シリアライズやエディタで使用）
    ModelComponent(const std::string& path) {
        SetModel(path);
    }

    // パターンB: 既存のポインタから生成（あなたの現在のコードで使用）
    ModelComponent(std::shared_ptr<Model> model) : modelPtr(model) {
        if (model) {
            // Modelクラスの GetPath() を使ってパスを同期する
            // これをしておかないと、セーブした時にパスが空文字になってしまう
            assetPath = model->GetPath();
        }
    }

    Model* GetModel() const { return modelPtr.get(); }


    void SetModel(const std::string& path) {
        // 1. パスを保存（シリアライズ用）
        assetPath = path;

        if (assetPath.empty()) {
            modelPtr = nullptr;
            return;
        }

        // 2. GraphicsシステムからDirectXデバイスを取得
        // ※ Graphics::GetDevice() という関数がある前提です。
        //    もし名前が違う場合 (GetD3DDeviceなど) は書き換えてください。
        ID3D11Device* device = Graphics::Instance().GetDevice();

        // 3. デバイスとパスを渡して Model を生成
        // Modelのコンストラクタ内で ResourceManager::LoadModelResource が呼ばれ、
        // 重いデータ（頂点など）は自動的にキャッシュ・共有されます。
        modelPtr = std::make_shared<Model>(device, assetPath.c_str());
    }
};
