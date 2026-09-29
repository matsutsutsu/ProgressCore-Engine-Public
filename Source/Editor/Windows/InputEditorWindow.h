#pragma once
// ===========================================================================
// 概要: 入力の割り当て（Assets/Config/InputConfig.json）を編集するウィンドウ。
//
// 責務:
//   - アクション（デジタル）と軸（アナログ）の追加・削除と、割り当ての編集
//   - 実際にキー・ボタンを押して割り当てる（キャプチャ）
//   - JSON の読み書きと、保存したものを InputFacade に読み直させる
// ===========================================================================
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

    // JSON と1対1で対応する編集用のデータ
    struct BindingData
    {
        std::string device = "Keyboard";
        std::string key    = "NONE";
        float       scale  = 1.0f;
    };

    struct ActionData
    {
        std::string              name;
        std::string              trigger = "Down"; // "Down" / "Triggered" / "Released"
        std::vector<BindingData> bindings;
    };

    struct AxisData
    {
        std::string              name;
        std::vector<BindingData> bindings;
    };

    std::vector<ActionData> m_actions;
    std::vector<AxisData>   m_axes;

    std::string m_configPath = "Assets/Config/InputConfig.json";

    // キャプチャ（キー入力待ち）の対象。nullptr ならキャプチャ中でない。
    //   m_actions / m_axes の要素を指すので、それらを追加・削除したら必ず nullptr に戻す
    BindingData *m_captureTarget = nullptr;

    // キャプチャを受け付け始めたか。キャプチャのボタンを押したクリックそのものを
    // 拾わないよう、マウスボタンが一度離されるまで待つ
    bool m_captureArmed = false;

    // 割り当て1つぶんを描く（デバイス / キー / キャプチャ / 軸なら感度）
    void DrawBindingParams(BindingData &b, bool isAxis);

    // デバイスの種類の選択（ラジオボタン）
    void DrawDeviceSelector(BindingData &b, bool isAxis);

    // 選択中のデバイスに応じたキーの候補をコンボボックスで出す
    void DrawKeyCombo(BindingData &b, bool isAxis);
};