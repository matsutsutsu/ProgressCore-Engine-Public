/// @file  CameraPathEditorWindow.h
/// @brief CameraDollyCartの軌道データ(Asset)を視覚的に編集し、JSONに直接書き出す専用ウィンドウ。

#pragma once
#include "Editor/Core/EditorWindow.h"
#include <string>
#include <vector>
#include <SimpleMath.h>

class CameraPathEditorWindow : public EditorWindow
{
  public:
    CameraPathEditorWindow();
    virtual ~CameraPathEditorWindow() = default;

  protected:
    void DrawContents(EditorContext &context) override;

  private:
    /// @brief 編集中の軌道をJSONアセットとしてファイルに上書き保存する
    void SavePathToJson(const std::string &relativePath, const std::vector<DirectX::SimpleMath::Vector3> &points);

    // 内部状態
    int  m_selectedPointIndex = -1;    // 現在選択している頂点のインデックス
    bool m_isAutoSave         = false; // (オプション) 値を変えたら自動保存するか
};