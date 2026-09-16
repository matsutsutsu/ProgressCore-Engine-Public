#include "TextEditorWindow.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Graphics/Renderer/ImGuiRenderer.h"

#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

TextEditorWindow::TextEditorWindow() : EditorWindow("Text Editor")
{
    // デフォルトパスの初期化
    //strcpy_s(m_filePath, sizeof(m_filePath), "Assets/Weapon/AttachmentMaster.json");
    strcpy_s(m_filePath, sizeof(m_filePath), "Shader/Material/Phong/PhongPS.hlsl");

    // 編集用のメモリバッファを大きめに確保（例：1MB = 1024 * 1024 バイト）
    // これだけあれば数万行のシェーダーコードでも余裕で収まります。
    m_textBuffer.resize(1024 * 1024, '\0');
}

void TextEditorWindow::DrawContents(EditorContext &context)
{
    // ---------------------------------------------------------
    // ① ファイルパスと操作ボタン領域
    // ---------------------------------------------------------
    ImGui::InputText("File Path", m_filePath, sizeof(m_filePath));
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
        if (auto path = FileDialog::OpenPreset("TextEditor", "Select File to Edit",
                                               Graphics::Instance().GetWindowHandle()))
        {
            const auto s = path->string();
            strcpy_s(m_filePath, sizeof(m_filePath), s.c_str());
            LoadFile(m_filePath);
        }
    }

    ImGui::Separator();

    // 操作ボタン群
    if (ImGui::Button("Load"))
    {
        LoadFile(m_filePath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save"))
    {
        SaveFile(m_filePath);
    }
    ImGui::SameLine();

    // ホットリロードボタン（ファイルに応じて処理を分ける）
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    if (ImGui::Button("Apply (Save & Reload)"))
    {
        SaveFile(m_filePath);

        // ファイルパスを文字列として判定
        std::string pathStr(m_filePath);

        // Luaスクリプトの場合の処理（必要になれば拡張してください）
        // else if (pathStr.find(".lua") != std::string::npos) {
        //     // TODO: Lua VMをリロードしたり、コンポーネントに再読み込み指令を出す
        // }
    }
    ImGui::PopStyleColor();

    ImGui::Separator();

    // ---------------------------------------------------------
    // ② テキストエディタ領域 (ImGui標準)
    // ---------------------------------------------------------
    // ウィンドウの残りの「縦横サイズ」を計算
    ImVec2 availableSize = ImGui::GetContentRegionAvail();

    // =========================================================
    // ここから下をエディタ専用フォント(Ricty)に切り替える！
    // =========================================================
    if (ImFont *editorFont = ImGuiRenderer::GetEditorFont())
    {
        ImGui::PushFont(editorFont);
    }

    // ImGui標準の複数行テキストボックスを描画！
    // ImGuiInputTextFlags_AllowTabInput を付けることで、Tabキーで空白が入力できるようになります。
    ImGui::InputTextMultiline("##TextEditor",
                              m_textBuffer.data(),
                              m_textBuffer.size(),
                              availableSize,
                              ImGuiInputTextFlags_AllowTabInput);

    // =========================================================
    // フォントの切り替えを元に戻す！
    // =========================================================
    if (ImGuiRenderer::GetEditorFont())
    {
        ImGui::PopFont();
    }
}

/**
 * @brief 指定されたテキストファイルをメモリバッファにロードします。
 * @param path 読み込むファイルの仮想パス
 */
void TextEditorWindow::LoadFile(const char *path)
{
    // VFSから読み込み
    std::string rawText = FileSystem::ReadAllText(path);
    if (!rawText.empty())
    {
        // もし読み込んだファイルがバッファよりデカかったら拡張する安全設計
        if (rawText.size() >= m_textBuffer.size())
        {
            m_textBuffer.resize(rawText.size() * 2, '\0');
        }

        // 古い文字列のゴミが残らないようにクリア
        std::fill(m_textBuffer.begin(), m_textBuffer.end(), '\0');

        // バッファに文字列をコピー
        std::copy(rawText.begin(), rawText.end(), m_textBuffer.begin());
    }
}

/**
 * @brief メモリバッファのテキストデータを指定されたファイルに保存します。
 * @param path 保存先の仮想パス
 */
void TextEditorWindow::SaveFile(const char *path)
{
    // VFSへ書き込み (バッファの生ポインタを渡す)
    FileSystem::WriteAllText(path, m_textBuffer.data());
}
