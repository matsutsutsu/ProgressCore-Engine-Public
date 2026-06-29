#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Engine/Core/Math/CurveData.h"

class CurveEditorWindow : public EditorWindow
{
  public:
    CurveEditorWindow()
        : EditorWindow("Curve Editor")
    {
    }
    virtual ~CurveEditorWindow() = default;

  protected:
    void DrawContents(EditorContext &context) override;

  private:
    CurveData   _currentCurve;
    std::string _currentFilePath  = "";
    int         _selectedKeyIndex = -1;

    // ImCurveEdit が直接書き換える「キャンバスの表示範囲」
    // （ユーザーがマウスホイール等でズームするとここが自動更新される）
    ImVec2 _curveViewMin = ImVec2(0.0f, -0.2f);
    ImVec2 _curveViewMax = ImVec2(1.0f, 1.2f);
};