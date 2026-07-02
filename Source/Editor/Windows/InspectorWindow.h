#pragma once
#include "Editor/Core/EditorWindow.h"

class InspectorWindow : public EditorWindow {
  public:
    InspectorWindow();

  protected:
    void DrawContents(EditorContext &context) override;

private:
    // 「+ Add Component」ポップアップ用の検索バッファ
    char _compSearchBuffer[256] = {0};

    // ① 付いているコンポーネントを絞り込むフィルタ
    char _inspectorFilterBuffer[256] = {0};

    // ② 一括開閉の一時指令: -1=なし / 0=全部閉じる / 1=全部開く
    int _forceOpenState = -1;

    // ⑤ Jump to の対象（このフレームでスクロールしたら消費）
    bool             _hasJumpTarget = false;
    CCL::ECS::TypeID _jumpToTypeID  = 0;

};