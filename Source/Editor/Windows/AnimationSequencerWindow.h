#pragma once
// ===================================================================================
// ファイル: AnimationSequencerWindow.h
// 役割       : AnimSequence(アニメーション台本)を時間軸上で編集するエディタウィンドウ。
// 責務       : ImSequencer によるイベント区間編集、CurveEditorWidget による速度/RootMotion
//              カーブ編集、選択エンティティへのリアルタイムプレビュー（速度・移動量とも反映）。
// 位置付け   : 編集対象は内部バッファ(_editingSequence)。保存で JSON 化し、ランタイムは
//              ResourceManager 経由で再ロードする(エディタ↔ランタイムは疎結合)。
// 設計方針   : 状態は全てインスタンスメンバ(static禁止)。編集モード突入/解除で
//              Animator の currentSequence を退避/復元し、実行時への汚染を防ぐ。
// ===================================================================================
#include "Editor/Core/EditorWindow.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/ECS/Common/CCL_Common.h"
#include "Editor/Utils/CurveEditorWidget.h" // パスは配置に合わせて調整
#include <DirectXMath.h>
#include <memory>
#include <string>

class AnimationSequenceImpl;
class AnimatorComponent;
class Model;

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
    // --- 描画・更新を分割したサブ関数群 ---
    void ResetStateOnEntitySwitch(EditorContext &context, AnimatorComponent *animator);
    void DrawFileIO();
    void DrawEditModeToggle(EditorContext &context, AnimatorComponent *animator);
    void DrawTimelinePane(EditorContext &context, AnimatorComponent *animator, Model *model);
    void DrawInspectorPane(EditorContext &context);
    void DrawCurvePane(float animTime);

    /// @brief 編集モード中、rootMotionCurve の進捗→累積前進距離を選択エンティティの
    ///        Transform に直接反映し、その場で移動量を確認できるようにする。
    /// @details ベース位置は「カーブ駆動プレビューが有効になった瞬間の位置」を1回だけ
    ///          記録し、以後は毎フレーム base + forward * (curve(progress) - curve(0))
    ///          で絶対位置を再計算する（スクラブしても加算ドリフトしない）。
    void DrawRootMotionPreview(EditorContext &context, AnimatorComponent *animator);

  private:
    // --- pImpl（ImSequencer 依存をcppに隠蔽） ---
    std::unique_ptr<AnimationSequenceImpl> _sequencerImpl;
    CurveEditorWidget                      _curveWidget; ///< 速度/RootMotionカーブの共通編集ウィジェット

    // --- ImSequencer 表示状態 ---
    bool _expanded      = true;
    int  _selectedEntry = -1;
    int  _firstFrame    = 0;
    bool _fitCurveView  = true; ///< 次フレームでカーブビューを全体表示にフィット

    // --- タイムライン操作の使い勝手 ---
    bool _snapEnabled = true; ///< イベント区間ドラッグ/ナッジのスナップ有効
    int  _snapStep    = 5;    ///< スナップ単位（フレーム）
    bool _loopPreview = true; ///< プレビュー再生のループ

    // --- 編集対象バッファ・UI状態（旧: ファイル/関数スコープ static） ---
    AnimSequence _editingSequence;               ///< 編集中の台本（旧: g_EditingSequence）
    std::string  _savePath;                      ///< 保存先パス（旧: g_SavePath）
    int          _selectedAnimIndex     = -1;    ///< 選択中アニメ（旧: s_SelectedAnimIndex）
    int          _prevSelectedAnimIndex = -2;    ///< コンボ変化検出（旧: prevSelectedIndex）
    bool         _isPlaying             = false; ///< プレビュー再生中（旧: s_isPlaying）
    bool         _wasEditMode           = false; ///< 前フレームEditMode（旧: s_wasEditMode）

    // --- 編集モード解除時の currentSequence 復元用 ---
    const AnimSequence *_savedSequence = nullptr;

    // --- ルートモーションカーブのプレビュー用状態 ---
    DirectX::XMFLOAT3 _rootMotionBasePos = {0.0f, 0.0f, 0.0f}; ///< プレビュー開始時点の位置（復元/基準に使用）
    bool              _rootMotionActive  = false;              ///< 今このフレーム、カーブ駆動で位置を書き換えているか

    // --- エンティティ切替検出 ---
    CCL::ECS::EntityID _lastSelectedEntity = CCL::ECS::InvalidEntityID;
};