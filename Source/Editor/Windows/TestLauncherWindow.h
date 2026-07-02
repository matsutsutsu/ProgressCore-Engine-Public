#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <d3d11.h>
#include <string>
#include <vector>
#include <functional>

class TestLauncherWindow : public EditorWindow {
public:
    TestLauncherWindow();

protected:
    void DrawContents(EditorContext& context) override;

private:
    // =======================================================
    // 形式ごとのテストケース定義
    // =======================================================

    // 1. 引数なしの基本テスト (Graphics, Animationなど)
    struct BasicTest {
        std::string name;
        std::function<void(CCL::ECS::Core::World&, ID3D11Device*)> callback;
    };

    // 2. 個数を指定できるパフォーマンステスト
    struct CountTest {
        std::string name;
        int count; // ★ UIで操作するための変数
        std::function<void(CCL::ECS::Core::World&, ID3D11Device*, int)> callback;
    };


    std::vector<BasicTest>   _basicTests;
    std::vector<CountTest>   _perfTests;
 
    // 共通の環境リセット処理
    void CleanupAndInitializeEnv(EditorContext& context);
};