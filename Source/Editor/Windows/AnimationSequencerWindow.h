#pragma once
// ===================================================================================
// ファイル: AnimationSequencerWindow.h
// 役割       : AnimSequence(アニメーション台本)を時間軸上で編集するエディタウィンドウ。
// 責務       : ImSequencer によるイベント区間編集、ImCurveEdit による速度/RootMotion
//              カーブ編集、選択エンティティへのリアルタイムプレビュー。
// 位置付け   : 編集対象は内部バッファ(_editingSequence)。保存で JSON 化し、ランタイムは
//              ResourceManager 経由で再ロードする(エディタ↔ランタイムは疎結合)。
// 設計方針   : 状態は全てインスタンスメンバ(static禁止)。編集モード突入/解除で
//              Animator の currentSequence を退避/復元し、実行時への汚染を防ぐ。
// ===================================================================================
#include "Editor/Core/EditorWindow.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/ECS/Common/CCL_Common.h"
#include <memory>
#include <string>

class AnimationSequenceImpl;
struct SequencerCurveDelegate;

/// @brief アニメーション台本(AnimSequence)編集ウィンドウ。
/// @details 1ウィンドウ=1編集バッファ。選択エンティティが変わると再バインドする。
class AnimationSequencerWindow : public EditorWindow
{
  public:
    AnimationSequencerWindow();
    virtual ~AnimationSequencerWindow();

  protected:
    void DrawContents(EditorContext &context) override;

  private:
    // --- pImpl（ImSequencer / ImCurveEdit 依存をcppに隠蔽） ---
    std::unique_ptr<AnimationSequenceImpl>  _sequencerImpl;
    std::unique_ptr<SequencerCurveDelegate> _curveDelegate;

    // --- ImSequencer 表示状態 ---
    bool _expanded      = true;
    int  _selectedEntry = -1;
    int  _firstFrame    = 0;
    bool _fitCurveView  = true;

    // --- 編集対象バッファ・UI状態（旧: ファイル/関数スコープ static） ---
    AnimSequence _editingSequence;               ///< 編集中の台本（旧: g_EditingSequence）
    std::string  _savePath;                      ///< 保存先パス（旧: g_SavePath）
    int          _selectedAnimIndex     = -1;    ///< 選択中アニメ（旧: s_SelectedAnimIndex）
    int          _prevSelectedAnimIndex = -2;    ///< コンボ変化検出（旧: prevSelectedIndex）
    bool         _isPlaying             = false; ///< プレビュー再生中（旧: s_isPlaying）
    bool         _wasEditMode           = false; ///< 前フレームEditMode（旧: s_wasEditMode）

    // --- 編集モード解除時の currentSequence 復元用 ---
    const AnimSequence *_savedSequence = nullptr;

    // --- エンティティ切替検出 ---
    CCL::ECS::EntityID _lastSelectedEntity = CCL::ECS::InvalidEntityID;
};