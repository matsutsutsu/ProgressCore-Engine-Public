#pragma once
// ===========================================================================
// 概要: テキストファイル（主にシェーダー）を開いて編集・保存する簡易エディタ
//
// 責務:
//   - ファイルの読み込み・保存（FileSystem 経由）
//   - 複数行のテキスト編集欄を描く
//
// 備考: シェーダーは ShaderRegistry がファイルの更新時刻を毎フレーム見ているので、
//       保存すれば自動で読み直される
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include <vector>

class TextEditorWindow : public EditorWindow
{
public:
    TextEditorWindow();
    virtual ~TextEditorWindow() = default;

protected:
    void DrawContents(EditorContext &context) override;

private:
    // path を読み込む。空のファイル・読み込めないファイルなら、バッファは前の内容のまま残す
    void LoadFile(const char *path);
    // バッファを '\0' までの文字列として path へ保存する
    void SaveFile(const char *path);

private:
    char              m_filePath[256]; // 編集中のファイルのパス
    std::vector<char> m_textBuffer;    // 編集用のバッファ（ImGui が直接書き込むので固定長で持つ）
};