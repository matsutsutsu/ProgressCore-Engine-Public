// Game/UI/PauseMenuUI.h
#pragma once
#include "Engine/UI/UIManager.h"
#include "Engine/UI/Widgets/SimpleButton.h"
#include "Engine/UI/Widgets/UIText.h"
#include <functional>
#include <memory>

// どのシーンでも使い回せる「ポーズメニュー」のプレハブクラス
class PauseMenuUI {
  private:
    std::shared_ptr<UIElement>    _rootPanel;
    std::shared_ptr<SimpleButton> _resumeBtn;
    std::shared_ptr<SimpleButton> _titleBtn;

    UIManager *_uiManager = nullptr;

  public:
    // 初期化とUIの構築
    void Initialize(UIManager *uiManager,
        std::function<void()>  resumeCallback,
        std::function<void()>  titleCallback)
    {
        _uiManager = uiManager;
        if (!_uiManager) return;

        // ★スプライト名を汎用的なものに変更
        _uiManager->LoadSprite(
            "PauseBg", "Assets/Textures/UI/PausePanel.png"); // 半透明の黒画像など
        _uiManager->LoadSprite("Btn_Menu", "Assets/Textures/UI/Button.png");

        // 1. 親パネル（画面全体を覆う板。入力をブロックする役割）
        _rootPanel = std::make_shared<UIElement>("PauseRoot", "PauseBg");
        _rootPanel->SetSize(1920, 1080);
        _rootPanel->SetPosition(960, 540);
        _rootPanel->SetVisible(false);

        // --- タイトルテキスト ---
        auto titleText = std::make_shared<UIText>("PauseTitleText", "PAUSED");
        titleText->SetSize(64, 64);
        titleText->SetPosition(0, -200);
        titleText->SetColor(1, 1, 1, 1);
        _rootPanel->AddChild(titleText);

        // --- 再開(CONTINUE)ボタン ---
        _resumeBtn = std::make_shared<SimpleButton>("ResumeBtn", "Btn_Menu", resumeCallback);
        _resumeBtn->SetSize(400, 100);
        _resumeBtn->SetPosition(0, 0);

        auto resumeText = std::make_shared<UIText>("ResumeText", "CONTINUE");
        resumeText->SetSize(48, 48);
        resumeText->SetPosition(0, 0);
        _resumeBtn->AddChild(resumeText);
        _rootPanel->AddChild(_resumeBtn);

        // --- タイトルへ(TITLE)ボタン ---
        _titleBtn = std::make_shared<SimpleButton>("TitleBtn", "Btn_Menu", titleCallback);
        _titleBtn->SetSize(400, 100);
        _titleBtn->SetPosition(0, 150);

        auto titleBtnText = std::make_shared<UIText>("TitleBtnText", "TITLE");
        titleBtnText->SetSize(48, 48);
        titleBtnText->SetPosition(0, 0);
        _titleBtn->AddChild(titleBtnText);
        _rootPanel->AddChild(_titleBtn);

        _uiManager->AddElement(_rootPanel);
    }

    ~PauseMenuUI()
    {
        if (_uiManager && _rootPanel) {
            _rootPanel->SetVisible(false); // 破棄時は安全に隠す
        }
    }

    void Show()
    {
        if (_rootPanel) _rootPanel->SetVisible(true);
    }
    void Hide()
    {
        if (_rootPanel) _rootPanel->SetVisible(false);
    }
};