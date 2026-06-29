#include "CurveEditorWindow.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <ImCurveEdit.h> // ★ 拡張ライブラリをインクルード

#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/Logger.h"
#include <json.hpp>
#include <algorithm>

namespace
{
// ===========================================================================
// アダプター: CurveData と ImCurveEdit を繋ぐデリゲート
// ===========================================================================
struct CurveDelegate : public ImCurveEdit::Delegate
{
    CurveData          *curveData;
    std::vector<ImVec2> cachedPoints;
    ImVec2             *pViewMin;
    ImVec2             *pViewMax;

    CurveDelegate(CurveData *data, ImVec2 *vMin, ImVec2 *vMax)
        : curveData(data), pViewMin(vMin), pViewMax(vMax)
    {
        SyncFromData();
    }

    // コアデータから ImVec2 のキャッシュへ同期
    void SyncFromData()
    {
        cachedPoints.clear();
        if (curveData)
        {
            for (const auto &k : curveData->keys)
            {
                cachedPoints.push_back(ImVec2(k.time, k.value));
            }
        }
    }

    // --- ImCurveEdit::Delegate の必須インターフェース実装 ---

    size_t GetCurveCount() override
    {
        return 1;
    }
    bool IsVisible(size_t) override
    {
        return true;
    }
    size_t GetPointCount(size_t) override
    {
        return cachedPoints.size();
    }
    uint32_t GetCurveColor(size_t) override
    {
        return IM_COL32(0, 255, 128, 255);
    }
    ImVec2 *GetPoints(size_t) override
    {
        return cachedPoints.data();
    }
    ImCurveEdit::CurveType GetCurveType(size_t) const override
    {
        return ImCurveEdit::CurveLinear;
    }

    int EditPoint(size_t curveIndex, int pointIndex, ImVec2 value) override
    {
        // ImCurveEditからのドラッグ入力をコアデータへ反映
        curveData->keys[pointIndex].time  = value.x;
        curveData->keys[pointIndex].value = value.y;

        // X軸（時間）が変化した可能性があるので再ソートする
        CurveKeyframe target = curveData->keys[pointIndex];
        std::sort(curveData->keys.begin(), curveData->keys.end(), [](const CurveKeyframe &a, const CurveKeyframe &b) {
            return a.time < b.time;
        });
        SyncFromData();

        // ソート後、編集したポイントの「新しいインデックス」を探して返す
        for (int i = 0; i < (int)curveData->keys.size(); ++i)
        {
            if (curveData->keys[i].time == target.time && curveData->keys[i].value == target.value)
            {
                return i;
            }
        }
        return pointIndex;
    }

    void AddPoint(size_t curveIndex, ImVec2 value) override
    {
        CurveKeyframe k;
        k.time   = value.x;
        k.value  = value.y;
        k.interp = CurveInterpolation::Linear;
        curveData->keys.push_back(k);

        std::sort(curveData->keys.begin(), curveData->keys.end(), [](const CurveKeyframe &a, const CurveKeyframe &b) {
            return a.time < b.time;
        });
        SyncFromData();
    }

    ImVec2 &GetMax() override
    {
        return *pViewMax;
    }
    ImVec2 &GetMin() override
    {
        return *pViewMin;
    }
    unsigned int GetBackgroundColor() override
    {
        return IM_COL32(30, 30, 30, 255);
    }
};
} // namespace

void CurveEditorWindow::DrawContents(EditorContext &context)
{
    // ========================================================
    // ★ 必須: ImCurveEditの仕様対策 (空のカーブを許さない)
    // ========================================================
    // 最初から線が存在しないとポイントを追加できないため、最低2つのキーを保証する
    auto InitializeDefaultCurve = [&]() {
        _currentCurve.keys.clear();
        CurveKeyframe k0;
        k0.time   = 0.0f;
        k0.value  = 0.0f;
        k0.interp = CurveInterpolation::Linear;
        CurveKeyframe k1;
        k1.time   = 1.0f;
        k1.value  = 1.0f;
        k1.interp = CurveInterpolation::Linear;
        _currentCurve.keys.push_back(k0);
        _currentCurve.keys.push_back(k1);
        _selectedKeyIndex = -1;
    };

    // 起動直後に空っぽなら初期化
    if (_currentCurve.keys.empty())
    {
        InitializeDefaultCurve();
    }

    // ========================================================
    // 1. ツールバー (ロード・セーブ)
    // ========================================================
    if (ImGui::Button("New"))
    {
        _currentCurve.keys.clear();
        _currentFilePath  = "";
        InitializeDefaultCurve(); // ★ 単なるclear()ではなく、2つのキーを配置する
    }

    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
        if (auto pathOpt = FileDialog::OpenPreset("CurveData", "Load Curve Data"))
        {
            std::string relativePath = std::filesystem::relative(*pathOpt, std::filesystem::current_path()).string();
            std::string jsonStr      = FileSystem::ReadAllText(relativePath);

            if (!jsonStr.empty())
            {
                try
                {
                    auto j             = nlohmann::json::parse(jsonStr);
                    _currentCurve.name = j.value("name", "");
                    _currentCurve.keys.clear();

                    if (j.contains("keys") && j["keys"].is_array())
                    {
                        for (const auto &item : j["keys"])
                        {
                            CurveKeyframe key;
                            key.time       = item.value("time", 0.0f);
                            key.value      = item.value("value", 0.0f);
                            key.inTangent  = item.value("inTangent", 0.0f);
                            key.outTangent = item.value("outTangent", 0.0f);
                            key.interp     = static_cast<CurveInterpolation>(item.value("interp", 1));

                            _currentCurve.keys.push_back(key);
                        }
                    }

                    std::sort(_currentCurve.keys.begin(), _currentCurve.keys.end(), [](const CurveKeyframe &a, const CurveKeyframe &b) {
                        return a.time < b.time;
                    });

                    _currentFilePath  = relativePath;
                    _selectedKeyIndex = -1;
                    CCL_LOG_INFO(LogCategory::Editor, "Loaded Curve: %s", relativePath.c_str());
                }
                catch (const std::exception &e)
                {
                    CCL_LOG_ERROR(LogCategory::Editor, "Failed to load curve data: %s", e.what());
                }
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Save"))
    {
        if (_currentFilePath.empty())
        {
            if (auto pathOpt = FileDialog::SavePreset("CurveData", "Save Curve Data", "NewCurve.json"))
            {
                _currentFilePath = std::filesystem::relative(*pathOpt, std::filesystem::current_path()).string();
            }
        }

        if (!_currentFilePath.empty())
        {
            try
            {
                nlohmann::json j;
                j["name"] = _currentCurve.name;
                j["keys"] = nlohmann::json::array();

                for (const auto &key : _currentCurve.keys)
                {
                    j["keys"].push_back({{"time", key.time},
                        {"value", key.value},
                        {"inTangent", key.inTangent},
                        {"outTangent", key.outTangent},
                        {"interp", static_cast<int>(key.interp)}});
                }

                FileSystem::WriteAllText(_currentFilePath, j.dump(4));
                CCL_LOG_INFO(LogCategory::Editor, "Saved Curve: %s", _currentFilePath.c_str());
            }
            catch (const std::exception &e)
            {
                CCL_LOG_ERROR(LogCategory::Editor, "Failed to save curve data: %s", e.what());
            }
        }
    }

    ImGui::SameLine();
    ImGui::Text("Target: %s", _currentFilePath.empty() ? "[ Unsaved ]" : _currentFilePath.c_str());
    ImGui::Separator();

    // ========================================================
    // ★ 操作方法の明示 (ツール開発の基本)
    // ========================================================
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), 
        "[操作] 線上をダブルクリック: キー追加 | 右ドラッグ: 視点移動(パン) | ホイール: ズーム");
    ImGui::Separator();

    // ========================================================
    // 2. ImCurveEdit によるキャンバス描画と編集
    // ========================================================
    CurveDelegate delegate(&_currentCurve, &_curveViewMin, &_curveViewMax);

    ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.y      = std::max(100.0f, canvasSize.y - 150.0f);

    // ImCurveEdit に渡す「現在選択されているポイント」の配列
    ImVector<ImCurveEdit::EditPoint> selection;
    if (_selectedKeyIndex >= 0 && _selectedKeyIndex < _currentCurve.keys.size())
    {
        ImCurveEdit::EditPoint ep;
        ep.curveIndex = 0;
        ep.pointIndex = _selectedKeyIndex;
        selection.push_back(ep);
    }

    // 拡張ライブラリが一撃で全ての描画・操作を処理する
    ImCurveEdit::Edit(delegate, canvasSize, 100, nullptr, &selection);

    // ========================================================
    // ★ 追加: 目盛りとグリッドのオーバーレイ描画 (Dynamic Scale)
    // ========================================================
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    // 縦軸（Value）の目盛り描画
    int vSteps = 5; // 何分割するか
    for (int i = 0; i <= vSteps; ++i)
    {
        float normalized = (float)i / vSteps;
        float val        = _curveViewMin.y + (_curveViewMax.y - _curveViewMin.y) * normalized;
        // 画面上ではY座標が反転する（下がMax、上がMinにならないよう補正）
        float y = canvasPos.y + canvasSize.y - (normalized * canvasSize.y);

        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", val);
        // テキストを描画
        drawList->AddText(ImVec2(canvasPos.x + 5, y - (i == vSteps ? 15 : 7)), IM_COL32(200, 200, 200, 255), buf);

        // うっすらと横線を引く
        if (i > 0 && i < vSteps)
        {
            drawList->AddLine(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + canvasSize.x, y), IM_COL32(255, 255, 255, 20));
        }
    }

    // 横軸（Time）の目盛り描画
    int hSteps = 10;
    for (int i = 0; i <= hSteps; ++i)
    {
        float normalized = (float)i / hSteps;
        float timeVal    = _curveViewMin.x + (_curveViewMax.x - _curveViewMin.x) * normalized;
        float x          = canvasPos.x + (normalized * canvasSize.x);

        char buf[32];
        snprintf(buf, sizeof(buf), "%.2fs", timeVal);
        // テキストを描画
        drawList->AddText(ImVec2(x + 5, canvasPos.y + canvasSize.y - 20), IM_COL32(200, 200, 200, 255), buf);

        // うっすらと縦線を引く
        if (i > 0 && i < hSteps)
        {
            drawList->AddLine(ImVec2(x, canvasPos.y), ImVec2(x, canvasPos.y + canvasSize.y), IM_COL32(255, 255, 255, 20));
        }
    }

    // ユーザー操作による選択状態の変動を同期
    if (!selection.empty())
    {
        _selectedKeyIndex = selection[0].pointIndex;
    }
    else
    {
        _selectedKeyIndex = -1;
    }

    // ========================================================
    // 3. インスペクタ (選択中キーの詳細設定)
    // ========================================================
    ImGui::Separator();
    if (_selectedKeyIndex >= 0 && _selectedKeyIndex < (int)_currentCurve.keys.size())
    {
        CurveKeyframe &key = _currentCurve.keys[_selectedKeyIndex];
        ImGui::Text("Selected Key [%d]", _selectedKeyIndex);

        bool timeChanged = ImGui::DragFloat("Time", &key.time, 0.01f);
        ImGui::DragFloat("Value", &key.value, 0.01f);

        int         interp        = (int)key.interp;
        const char *interpNames[] = {"Constant", "Linear", "Cubic"};
        if (ImGui::Combo("Interpolation", &interp, interpNames, 3))
        {
            key.interp = (CurveInterpolation)interp;
        }

        if (key.interp == CurveInterpolation::Cubic)
        {
            ImGui::DragFloat("In Tangent", &key.inTangent, 0.1f);
            ImGui::DragFloat("Out Tangent", &key.outTangent, 0.1f);
        }

        if (ImGui::Button("Delete Key"))
        {
            _currentCurve.keys.erase(_currentCurve.keys.begin() + _selectedKeyIndex);
            _selectedKeyIndex = -1;
        }
        else if (timeChanged)
        {
            std::sort(_currentCurve.keys.begin(), _currentCurve.keys.end(), [](const CurveKeyframe &a, const CurveKeyframe &b) {
                return a.time < b.time;
            });
            _selectedKeyIndex = -1;
        }
    }
    else
    {
        ImGui::TextDisabled("No key selected. Double click on canvas to add a key.");
    }
}