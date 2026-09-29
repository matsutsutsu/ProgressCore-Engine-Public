#pragma once
// ===========================================================================
// 概要: カメラ台車（CameraDollyCart）の軌道アセットを編集し、JSON に保存するウィンドウ。
//
// 責務:
//   - 制御点の追加・挿入・削除と座標の数値編集、ループ設定
//   - 変更したら LUT（距離→t の対応表）を作り直す
//   - 選択中の制御点を EditorContext.editingPathNodeIndex に書く（GameViewWindow がギズモで動かす）
// ===========================================================================
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
    /**
     * @brief 編集中の軌道を JSON ファイルに上書き保存する。
     *
     * points だけを受け取る形にしない。保存のたびに isLoop が JSON から消え、
     * ループ設定が失われる。アセットごと受け取り、点と付帯情報を両方書き出す。
     *
     * GameViewWindow のギズモ操作からも呼ぶので static にしている。
     * 書き出し処理が2か所に分かれると、片方だけキーを足してもう片方が消す、
     * という不具合が起きる。
     */
    static void SavePathToJson(const std::string &relativePath, const CameraPathAsset &asset);

protected:
    void DrawContents(EditorContext &context) override;

private:
    int  m_selectedPointIndex = -1;    // 選択中の制御点の添字。-1 なら未選択
    bool m_isAutoSave         = false; // 値を変えたら自動で保存するか
};