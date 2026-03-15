#pragma once
#include "Engine/GamePlay/Core/Scene/SceneManager.h" // PlayerData用
#include "Engine/UI/UIManager.h"
#include "Engine/UI/Widgets/UIProgressBar.h"
#include "Engine/UI/Widgets/UIText.h"
#include <memory>
#include "ECS/Core/CCL_World.h"

// ECSとUIの間で受け渡しをするための純粋なデータ（POD）
struct PlayerHUDData {
    float hpRatio = 1.0f;
    int   money   = 0;
    float intervalRatio = 1.0f; // 銃のチャージ率 (0.0 ~ 1.0)
};

class PlayerHUD {
  private:
    // 管理するUIパーツ
    std::shared_ptr<UIProgressBar> _hpBar;
    std::shared_ptr<UIProgressBar> _intervalBar;
    std::shared_ptr<UIElement>     _moneyIcon;
    std::shared_ptr<UIText>        _moneyText;

  public:
    // 初期化：UIManagerを受け取って、そこにパーツを登録する
    void Initialize(UIManager *uiManager)
    {
        if (!uiManager) return;

        // 画像ロード (もしシーン側でロード済みなら不要だが、念のためここでも呼んでOK)
        uiManager->LoadSprite("HP_Fill", "Assets/Textures/UI/HUD/HPGauge.png");
        uiManager->LoadSprite("GunInterval", "Assets/Textures/UI/HUD/AttackGauge.png");
        uiManager->LoadSprite("CoinIcon", "Assets/Textures/UI/HUD/Coin.png");

        // --- HP Bar ---
        _hpBar = std::make_shared<UIProgressBar>("HPBar", "HP_Fill");
        _hpBar->SetSize(400, 30);
        _hpBar->InitProgress();
        _hpBar->SetPosition(300, -100); // 初期位置
        uiManager->AddElement(_hpBar);


        // --- Interval Bar ---
        _intervalBar = std::make_shared<UIProgressBar>("IntervalBar", "GunInterval");
        _intervalBar->SetSize(300, 30);
        _intervalBar->InitProgress();
        _intervalBar->SetPosition(-300, 150);
        uiManager->AddElement(_intervalBar);

        // --- Money UI ---
        _moneyIcon = std::make_shared<UIElement>("CoinUI", "CoinIcon");
        _moneyIcon->SetSize(64, 64);
        _moneyIcon->SetPosition(100, 1200); // 画面外
        uiManager->AddElement(_moneyIcon);

        _moneyText = std::make_shared<UIText>("MoneyText", "0");
        //_moneyText->SetSize(400, 400);
        _moneyText->SetSize(64, 64);
        _moneyText->SetColor(1, 1, 1, 1);
        _moneyText->SetCenter(false);
        _moneyText->SetPosition(50, -70);
        _moneyIcon->AddChild(_moneyText); // 親子関係
    }

    // 表示アニメーション（シーン開始時に呼ぶ）
    void Show()
    {
        if (_hpBar) _hpBar->MoveTo(300, 100);
        if (_intervalBar) _intervalBar->MoveTo(245, 150);
        if (_moneyIcon) _moneyIcon->MoveTo(100, 980); // 画面内へ(Y座標は適宜調整)
    }

    // 非表示アニメーション
    void Hide()
    {
        if (_hpBar) _hpBar->MoveTo(300, -100);
        if (_intervalBar) _intervalBar->MoveTo(-300, 150);
        if (_moneyIcon) _moneyIcon->MoveTo(100, 1200);
    }

    // データ更新 (毎フレーム、または変更時に呼ぶ)
    void Update(const PlayerPersistentData &data, CCL::ECS::Core::World *world)
    {
        if (!world) return;

        if (!world->HasResource<PlayerHUDData>()) return;

        // 1. 掲示板からデータを読み取る
        auto &hudData = world->GetResource<PlayerHUDData>();


        // 2. UIウィジェットにデータを反映させる
        if (_hpBar) {
            _hpBar->SetProgress(hudData.hpRatio);
        }

        // インターバルバー（既に宣言されている _intervalBar）に数値を流し込む
        if (_intervalBar) {
            _intervalBar->SetProgress(hudData.intervalRatio);
        }

        // 所持金更新
        if (_moneyText) {
            _moneyText->SetText(std::to_string(data.money));
        }

    }

    // 攻撃インターバルなどの更新用（必要なら追加）
    void SetIntervalProgress(float ratio)
    {
        if (_intervalBar) _intervalBar->SetProgress(ratio);
    }
};