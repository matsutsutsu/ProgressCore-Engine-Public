#pragma once
// ===========================================================================
// 概要: 選択中のエンティティのコンポーネントを表示・編集するウィンドウ。
//
// 責務:
//   - 名前の編集、複製・削除、プレハブとしての保存
//   - コンポーネントをカテゴリごとにまとめて描く（検索・一括開閉つき）
//   - コンポーネントの追加・削除
//
// 備考: 描画内容は ComponentGuiRegistry に登録された関数に任せる。
//       編集するのは主選択の1体だけで、複製・削除は選択全体に適用する。
// ===========================================================================
#include "Editor/Core/EditorWindow.h"

class InspectorWindow : public EditorWindow
{
public:
    InspectorWindow();

protected:
    void DrawContents(EditorContext &context) override;

private:
    // 「+ Add Component」ポップアップ用の検索バッファ
    char m_compSearchBuffer[256] = {0};

    // 付いているコンポーネントを絞り込む検索文字列
    char m_inspectorFilterBuffer[256] = {0};

    // 一括開閉の指示（そのフレームだけ有効）: -1=なし / 0=全部閉じる / 1=全部開く
    int m_forceOpenState = -1;
};