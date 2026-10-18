#pragma once
// ===========================================================================
// 概要: AnimSequence（アニメーションの台本）を時間軸上で編集するウィンドウ。
//
// 責務:
//   - ImSequencer によるイベント区間（攻撃判定・軌跡・音など）の編集
//   - CurveEditorWidget による速度・ルートモーションのカーブの編集
//   - 選択中のエンティティでのプレビュー（姿勢・移動量・判定球）
//   - プレビュー中のポーズの固定（複数体を止めたまま、カメラを動かして構図を作る）
//   - JSON の読み書き
//
// 備考:
//   編集対象は内部のバッファ（m_editingSequence）。保存すると ResourceManager の同じ
//   インスタンスへ読み直させ、ゲームの再生に反映する。
//   編集モードに入るときに Animator の currentSequence を退避し、抜けるときに戻す。
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/ECS/Common/CCL_Types.h"
#include "Editor/Utils/CurveEditorWidget.h" // 速度カーブ編集ウィジェット
#include <DirectXMath.h>
#include <memory>
#include <string>
#include <vector>

class AnimationSequenceImpl;
struct AnimatorComponent; // 実体は struct
class Model;

// 1ウィンドウ=1編集バッファ。選択エンティティが変わると編集中の内容を捨てて作り直す。
class AnimationSequencerWindow : public EditorWindow
{
public:
    AnimationSequencerWindow();
    virtual ~AnimationSequencerWindow();

protected:
    void DrawContents(EditorContext &context) override;

    /**
     * @brief ウィンドウが描画されなくなっても編集モードを確実に解除する。
     *
     * 編集モードは Animator の isEditorOverride を立て、対象エンティティのアニメ更新を全システムで止める。
     * DrawContents はウィンドウを閉じる・畳む・選択を外すと呼ばれないため、解除をそこに置くと状態が残り続けて「そのエンティティのアニメが二度と動かない」になる。
     */
    void OnFrameBegin(EditorContext &context) override;

private:
    // 選択中のエンティティが変わったら、編集中の内容を捨てて作り直す
    void ResetStateOnEntitySwitch(EditorContext &context);

    /**
     * @brief 編集モードを解除し、対象エンティティを編集前の状態へ戻す。
     * @param target 解除対象。切替時は「切替前」のエンティティを渡すこと
     *               （選択中のものへ復元すると、別モデルの台本を差し込んでしまう）。
     */
    void ReleaseEditMode(EditorContext &context, CCL::ECS::EntityID target);

    // ウィンドウ側の編集モードの状態だけを片付ける（エンティティには触らない）。解除と固定の両方が通る
    void ResetEditModeState(EditorContext &context);
    void DrawFileIO();
    void DrawEditModeToggle(EditorContext &context, AnimatorComponent *animator);
    void DrawTimelinePane(EditorContext &context, AnimatorComponent *animator, Model *model);
    void DrawInspectorPane(EditorContext &context);
    void DrawCurvePane(float animTime);

    // Sound イベントの音を、AudioConfig に登録済みのイベント名から選ぶ（手入力も可）。変えたら true
    bool DrawSoundPicker(EditorContext &context, AnimNotifyEvent &e);

    // Effect イベントの VFX を、Assets/Prefabs/Particles のプレハブから選ぶ（手入力も可）。変えたら true
    bool DrawVfxPrefabPicker(AnimNotifyEvent &e);

    /**
     * @brief 編集モード中、rootMotionCurve の進捗→累積前進距離を選択エンティティの Transform に直接反映し、その場で移動量を確認できるようにする。
     *
     * ベース位置は「カーブ駆動プレビューが有効になった瞬間の位置」を1回だけ記録する。
     * 以後は毎フレーム base + forward * (curve(progress) - curve(0)) で絶対位置を再計算する（スクラブしても加算ドリフトしない）。
     *
     * @param context  エディタの文脈。
     * @param animator 選択エンティティの Animator。
     */
    void DrawRootMotionPreview(EditorContext &context, AnimatorComponent *animator);

    /**
     * @brief 編集モード中、SpawnHitbox の判定球を GameView に線で描き、大きさと位置を確かめられるようにする。
     *
     * 選んでいるイベントは常に、それ以外は今の再生時刻が区間に入っているときだけ描く。
     *
     * @param context エディタの文脈。
     */
    void DrawHitboxPreview(EditorContext &context);

    // --- ポーズの固定 ---

    /**
     * @brief 編集モードで見ている今のポーズのまま、選択中のエンティティを止めておく。
     *
     * isEditorOverride を立てたまま編集モードだけを抜ける。編集モードを抜けると uiInteract が下りるので、フリーカメラが動かせる。
     * 台本は写しを固定側が持つ（m_editingSequence は選択を変えると作り直されるため、指したままにはできない）。
     */
    void PinCurrentPose(EditorContext &context, AnimatorComponent *animator);

    /**
     * @brief 固定を解き、そのエンティティを固定前（編集モードに入る前）の状態へ戻す。
     * @param index m_pinnedPoses の添字。
     */
    void UnpinPose(EditorContext &context, size_t index);

    // 固定を全部解く
    void UnpinAllPoses(EditorContext &context);

    // 指定エンティティの固定の添字を返す。固定していなければ -1
    int FindPinnedPose(CCL::ECS::EntityID entity) const;

    /**
     * @brief 消えたエンティティ・別物に置き換わったエンティティの固定を捨てる。
     *
     * シーンの読み直しで同じ ID が別のエンティティに使われると、そのエンティティを戻してしまう。
     * Animator が固定側の台本の写しを指していなければ、もう固定したものではないとみなす。
     */
    void PrunePinnedPoses(EditorContext &context);

    // 固定中のエンティティの一覧と、解除のボタンを描く
    void DrawPinnedPoseList(EditorContext &context);

private:
    // 固定中の1体分
    struct PinnedPose
    {
        CCL::ECS::EntityID            entity = CCL::ECS::InvalidEntityID; // 固定しているエンティティ
        std::unique_ptr<AnimSequence> sequence;                           // 固定中に Animator が指す台本の写し
        const AnimSequence           *savedSequence = nullptr;            // 解除時に戻す、編集モードに入る前の台本
        float                         time          = 0.0f;               // 固定した再生位置（秒）
        DirectX::XMFLOAT3             originPos     = {0.0f, 0.0f, 0.0f}; // 解除時に戻す位置
        bool                          restorePos    = false;              // ルートモーションのプレビューで動かしていたか（解除時に originPos へ戻す）
    };

    // ImSequencer への橋渡し。ImSequencer への依存を .cpp に閉じ込めるため pImpl にする
    std::unique_ptr<AnimationSequenceImpl> m_sequencerImpl;
    CurveEditorWidget                      m_curveWidget; // 速度・ルートモーションのカーブの編集
    bool                                   m_showSpeedCurve = true; // 速度カーブを描くか
    bool                                   m_showRootCurve  = true; // ルートモーションのカーブを描くか

    // --- ImSequencer 表示状態 ---
    bool m_expanded      = true;
    int  m_selectedEntry = -1;
    int  m_firstFrame    = 0;
    bool m_fitCurveView  = true; // 次のフレームでカーブの表示範囲を全体に合わせる

    // --- タイムライン操作の使い勝手 ---
    bool m_snapEnabled = true; // イベント区間をドラッグしたときにフレームへ吸着させるか
    int  m_snapStep    = 5;    // スナップ単位（フレーム）
    bool m_loopPreview = true; // プレビュー再生のループ

    // --- 編集対象のバッファと UI の状態 ---
    AnimSequence m_editingSequence;               // 編集中の台本
    std::string  m_savePath;                      // 保存先のパス
    int          m_selectedAnimIndex     = -1;    // 選択中のアニメーション
    int          m_prevSelectedAnimIndex = -2;    // コンボの変化の検出用。-2 は「読み込み直後で未同期」
    bool         m_isPlaying             = false; // プレビューを再生中か
    bool         m_wasEditMode           = false; // 前のフレームで編集モードだったか

    // --- 編集モード解除時の currentSequence 復元用 ---
    const AnimSequence *m_savedSequence = nullptr;

    // --- ルートモーションカーブのプレビュー用状態 ---
    DirectX::XMFLOAT3 m_rootMotionOriginPos = {0.0f, 0.0f, 0.0f}; // プレビュー開始時点の位置（終了時の復元用。累積しない）
    DirectX::XMFLOAT3 m_rootMotionBasePos   = {0.0f, 0.0f, 0.0f}; // 現在ループの基準位置（ループごとに1周ぶん前進を畳み込む）
    bool              m_rootMotionActive    = false;              // 今このフレーム、カーブ駆動で位置を書き換えているか

    // --- エンティティ切替検出 ---
    CCL::ECS::EntityID m_lastSelectedEntity = CCL::ECS::InvalidEntityID;

    // 編集モードで isEditorOverride を立てているエンティティ。Invalid なら立てていない。
    CCL::ECS::EntityID m_editingEntity = CCL::ECS::InvalidEntityID;

    // このフレームに DrawContents が編集モードを維持したか。
    // 維持されないフレームが来たら OnFrameBegin が解除する。
    bool m_editModeRefreshed = false;

    // 固定中のポーズ。ウィンドウを閉じても、選択を変えても解かない（解くのは解除のボタンだけ）
    std::vector<PinnedPose> m_pinnedPoses;
};