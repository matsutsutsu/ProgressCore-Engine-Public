/// @file  CameraPathEditorWindow.h
/// @brief CameraDollyCartの軌道データ(Asset)を視覚的に編集し、JSONに直接書き出す専用ウィンドウ。

#pragma once
#include "Engine/Camera/Components/CameraDollyCartComponent.h"
#include "Editor/Core/EditorWindow.h"
#include <string>
#include <vector>
#include <SimpleMath.h>

class CameraPathEditorWindow : public EditorWindow
{
  public:
    CameraPathEditorWindow();
    virtual ~CameraPathEditorWindow() = default;

  public:
    /// @brief 編集中の軌道をJSONアセットとしてファイルに上書き保存する。
    /// @note  以前は points だけを受け取っていたため、保存するたびに isLoop が
    ///        JSON から消えていた（＝ループ設定がエディタ経由で失われる）。
    ///        アセットごと受け取り、点群と付帯情報を両方書き出す。
    /// @note  GameViewWindow のギズモ操作からも呼ぶので static。
    ///        書き出し処理が2箇所に分かれていると、片方だけキーを足して
    ///        もう片方が消す、という事故が起きる（実際に起きていた）。
    static void SavePathToJson(const std::string &relativePath, const CameraPathAsset &asset);

  protected:
    void DrawContents(EditorContext &context) override;

  private:

    // 内部状態
    int  m_selectedPointIndex = -1;    // 現在選択している頂点のインデックス
    bool m_isAutoSave         = false; // (オプション) 値を変えたら自動保存するか
};