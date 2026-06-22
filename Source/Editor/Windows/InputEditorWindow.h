#pragma once
#include "Editor/Core/EditorWindow.h"
#include <vector>
#include <string>

class InputEditorWindow : public EditorWindow
{
  public:
    InputEditorWindow();
    virtual ~InputEditorWindow() = default;

  protected:
    void DrawContents(EditorContext &context) override;

  private:
    void LoadFromJson();
    void SaveToJson();

    // 内部データ構造 (JSONと1対1で対応するエディタ用データ)
    struct BindingData
    {
        std::string device = "Keyboard";
        std::string key    = "NONE";
        float       scale  = 1.0f;
    };

    struct ActionData
    {
        std::string              name;
        std::vector<BindingData> bindings;
    };

    struct AxisData
    {
        std::string              name;
        std::vector<BindingData> bindings;
    };

    std::vector<ActionData> _actions;
    std::vector<AxisData>   _axes;

    std::string _configPath = "Assets/Config/InputConfig.json";

    // --- UI状態 ---
    // 「キー押下キャプチャ」を待っている対象バインディング。
    // nullptr のときはキャプチャ中でない。
    BindingData *_captureTarget = nullptr;

    // 1フレーム前のキャプチャ抑制用（ボタンを押した瞬間に確定しないように）
    bool _captureArmed = false;

    // --- 描画ヘルパー ---
    // 1つのバインディング行を描画する（Device / Key / Scale / Capture）
    void DrawBindingParams(BindingData &b, bool isAxis);

    // デバイス種別の選択（ラジオボタン）
    void DrawDeviceSelector(BindingData &b, bool isAxis);

    // 選択中デバイスに応じたキー候補のコンボボックスを描画
    void DrawKeyCombo(BindingData &b, bool isAxis);
};