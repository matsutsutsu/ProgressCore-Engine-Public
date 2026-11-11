#include "Editor/Windows/Game/TextEditorWindow.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/FileSystem.h"

#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

TextEditorWindow::TextEditorWindow() : EditorWindow("Text Editor")
{
    strcpy_s(m_filePath, sizeof(m_filePath), "Shader/Material/Phong/PhongPS.hlsl");

    // 1MB 確保する。数万行のシェーダーでも収まる（超えるファイルは LoadFile で広げる）
    m_textBuffer.resize(1024 * 1024, '\0');
}



void TextEditorWindow::DrawContents(EditorContext &context)
{
    // ---- ファイルパスと操作ボタン ----
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

    // 保存するだけで、読み直しはシェーダー側が更新時刻を見て自動で行う
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    if (ImGui::Button("Apply (Save & Reload)"))
    {
        SaveFile(m_filePath);
    }
    ImGui::PopStyleColor();

    ImGui::Separator();

    // ---- テキストの編集欄（ウィンドウの残り全体） ----
    ImVec2 availableSize = ImGui::GetContentRegionAvail();

    // AllowTabInput を付けると、Tab キーでタブ文字を入力できる
    ImGui::InputTextMultiline("##TextEditor",
                              m_textBuffer.data(),
                              m_textBuffer.size(),
                              availableSize,
                              ImGuiInputTextFlags_AllowTabInput);
}



void TextEditorWindow::LoadFile(const char *path)
{
    std::string rawText = FileSystem::ReadAllText(path);
    if (!rawText.empty())
    {
        // バッファより大きいファイルなら広げる（終端の '\0' の分も要るので >=）
        if (rawText.size() >= m_textBuffer.size())
        {
            m_textBuffer.resize(rawText.size() * 2, '\0');
        }

        // 前の内容が後ろに残らないよう、全体を '\0' で埋めてからコピーする
        std::fill(m_textBuffer.begin(), m_textBuffer.end(), '\0');
        std::copy(rawText.begin(), rawText.end(), m_textBuffer.begin());
    }
}



void TextEditorWindow::SaveFile(const char *path)
{
    FileSystem::WriteAllText(path, m_textBuffer.data());
}
