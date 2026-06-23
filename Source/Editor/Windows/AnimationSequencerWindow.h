#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/ECS/Common/CCL_Common.h"
#include <memory>
#include <string>

class AnimationSequenceImpl;
struct SequencerCurveDelegate;

class AnimationSequencerWindow : public EditorWindow
{
  public:
    AnimationSequencerWindow();
    virtual ~AnimationSequencerWindow();

  protected:
    void DrawContents(EditorContext &context) override;

  private:
    // =====================================================================
    // pImpl（ImSequencer・ImCurveEdit の仲介）
    // =====================================================================
    std::unique_ptr<AnimationSequenceImpl>  _sequencerImpl;
    std::unique_ptr<SequencerCurveDelegate> _curveDelegate;

    // =====================================================================
    // ImSequencer の表示状態
    // =====================================================================
    bool _expanded      = true;
    int  _selectedEntry = -1;
    int  _firstFrame    = 0;
    bool _fitCurveView  = true;

    // =====================================================================
    // 【メンバ化】旧: ファイルスコープ static / 関数スコープ static
    // static はウィンドウ2枚・シーン再生成で状態が汚染されるため排除する。
    // =====================================================================

    /// 編集中のシーケンスデータ（旧: g_EditingSequence）
    AnimSequence _editingSequence;

    /// 保存先パス（旧: g_SavePath[256]）
    std::string _savePath;

    /// 選択中アニメーションインデックス（旧: s_SelectedAnimIndex）
    int _selectedAnimIndex = -1;

    /// 前フレームの選択インデックス（コンボ変化検出用）（旧: prevSelectedIndex）
    int _prevSelectedAnimIndex = -2;

    /// プレビュー再生中か（旧: s_isPlaying）
    bool _isPlaying = false;

    /// 前フレームに EditMode だったか（旧: s_wasEditMode）
    bool _wasEditMode = false;

    // =====================================================================
    // 【実バグ修正】編集モード解除時に currentSequence を元に戻すための退避先
    // 編集モード突入時に animator->currentSequence を保存し、
    // 解除時にそのポインタを復元する。
    // =====================================================================
    const AnimSequence *_savedSequence = nullptr;

    // =====================================================================
    // エンティティ切り替え検出用
    // selectedEntity が変わったら編集バッファを再バインドする。
    // =====================================================================
    CCL::ECS::EntityID _lastSelectedEntity = CCL::ECS::InvalidEntityID;
};