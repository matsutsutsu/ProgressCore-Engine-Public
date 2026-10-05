#pragma once
// ===========================================================================
// 概要: タイムライン（ムービー演出）の資産を時間軸上で編集するウィンドウ。
//
// 責務:
//   - タイムライン JSON の新規作成・読み込み・保存
//   - 5本の行（カメラ・アニメ・エフェクト・SE・シグナル）にクリップを並べ、ドラッグで時刻と長さを変える
//   - 選んだクリップの詳細の編集と、クリップの追加・削除
//   - プレビュー：再生位置のスクラブと、最初からの再生
//   - 今の画面のカメラの位置を、カメラショットの開始・終了の位置として取り込む
//
// 備考:
//   時間軸は ImSequencer を使わず、固定の大きさで自前に描く。ImSequencer は1コマを固定の幅で描くので、
//   長いムービーほど横に伸び、しかも1クリップ1行で縦にも伸びる。ここでは全体の長さがいつも
//   決まった幅に収まるように縮尺を変え、行は種類ごとの5本に固定する。
//
//   プレビュー先の TimelinePlayer は自動で用意する。編集中のファイルを再生するものが居ればそれを使い、
//   居なければ（EditorScene など）保存されない一時的なものを置く。
//
//   編集対象はウィンドウ内のバッファ（m_buffer）。保存するまでファイルは変わらない。
//   バッファ自体は並べ替えない（選んでいるクリップの番号がずれる）。並べ替えはプレビューへ渡すコピーと
//   保存時にだけ行う。元に戻す（Undo）は持たない。
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Timeline/TimelineAsset.h"

#include <cstdint>
#include <string>
#include <vector>

struct TimelinePlayerComponent;

// クリップの種類。時間軸の行の並び順もこの順
enum class TimelineClipKind : std::uint8_t
{
    Camera,
    Animation,
    Effect,
    Sound,
    Signal,
    Count
};

// クリップへの参照（種類と、その種類の配列の添字）
struct TimelineClipRef
{
    TimelineClipKind kind  = TimelineClipKind::Camera;
    int              index = -1;

    bool operator==(const TimelineClipRef &o) const { return kind == o.kind && index == o.index; }
    bool IsValid() const { return index >= 0; }
};

class TimelineEditorWindow : public EditorWindow
{
public:
    TimelineEditorWindow();
    ~TimelineEditorWindow() override = default;

protected:
    void OnBeforeBegin() override;
    void DrawContents(EditorContext &context) override;

private:
    // 時間軸の上でいま何を掴んでいるか
    enum class DragMode
    {
        None,
        Scrub,  // 目盛り・空いた所を掴んで再生位置を動かしている
        Move,   // クリップを丸ごと動かしている
        Resize, // クリップの右端を掴んで長さを変えている
    };

    // 未保存の変更を捨ててよいか確かめてから行う操作
    enum class PendingAction
    {
        None,
        New,
        Open,
    };

    // 時間軸に描く1クリップの情報（種類によらず同じ形にそろえる）
    struct ClipView
    {
        TimelineClipRef ref;
        float           start     = 0.0f;  // 開始(秒)
        float           length    = 0.0f;  // 長さ(秒)。点のクリップは 0
        bool            resizable = false; // 右端を掴んで長さを変えられるか
        std::string     label;             // 帯に書く短い名前
    };

    // ---- 上の帯：ファイルと再生 ----
    void DrawToolbar(EditorContext &context);
    void NewFile();
    void OpenFile();
    bool SaveFile(bool saveAs);
    void RequestAction(PendingAction action);
    void DrawDiscardPopup();

    // ---- プレビュー先 ----
    // 編集中のファイルを再生する TimelinePlayer を探し、居なければ一時的なものを置く
    void EnsurePreviewPlayer(EditorContext &context);
    TimelinePlayerComponent *PreviewPlayer(EditorContext &context) const;
    void ApplyToPlayer(EditorContext &context);
    void Scrub(EditorContext &context, float time);
    void StopPreview(EditorContext &context);
    // 役割が結び付いていなければ、欠けている名前を1行で出す
    void DrawRoleWarning(EditorContext &context);

    // ---- 時間軸 ----
    void DrawTimeline(EditorContext &context);
    std::vector<ClipView> CollectClips() const;
    void MoveClip(const TimelineClipRef &ref, float start);
    void ResizeClip(const TimelineClipRef &ref, float length);
    void AddClip(TimelineClipKind kind);
    void DeleteSelected();
    // 編集したことを記録する（未保存の印・プレビューへの反映待ち・全体の長さの再計算）
    void MarkEdited();

    // ---- 詳細 ----
    void DrawInspector(EditorContext &context);
    bool DrawCameraShot(EditorContext &context, CameraShotClip &c);
    bool DrawAnimation(AnimationClip &c);
    bool DrawEffect(EffectClip &c);
    bool DrawSound(EditorContext &context, SoundClip &c);
    bool DrawSignal(SignalClip &c);

    /**
     * @brief 今の画面のカメラの位置と向きを、ショットの開始か終了の値として取り込む。
     * @param shot    書き込むショット。Truck なら eye/look、Orbit なら角度・距離・高さと look。
     * @param toStart true なら開始、false なら終了の値へ書く。
     * @return 取り込めたら true。役割が結び付いていない・カメラが無いときは false。
     */
    bool CapturePose(EditorContext &context, CameraShotClip &shot, bool toStart) const;

    // 役割名からプレビュー先の実体の位置と向き（ラジアン、atan2(x, z)）を引く
    bool ResolveRole(EditorContext &context, const std::string &role, DirectX::SimpleMath::Vector3 &pos,
                     float &yaw) const;

    TimelineAsset   m_buffer;
    std::string     m_path;            // 編集中のファイル（空なら未保存の新規）
    bool            m_loaded  = false; // バッファに何か入っているか
    bool            m_dirty   = false; // 保存していない変更があるか
    PendingAction   m_pending = PendingAction::None;
    TimelineClipRef m_selected;        // 選んでいるクリップ
    float           m_time = 0.0f;     // 再生位置(秒)

    // 時間軸のドラッグ
    DragMode        m_drag = DragMode::None;
    TimelineClipRef m_dragRef;             // 掴んでいるクリップ
    float           m_dragOffset = 0.0f;   // 掴んだ点とクリップの開始の差(秒)。動かしても掴んだ所がずれないように

    // プレビュー先
    CCL::ECS::EntityID m_player     = CCL::ECS::InvalidEntityID; // いま使っている TimelinePlayer
    CCL::ECS::EntityID m_spawned    = CCL::ECS::InvalidEntityID; // このウィンドウが置いた一時的な TimelinePlayer
    int                m_spawnWait  = 0;     // 置いてから World に現れるまで待っているフレーム数
    bool               m_needsApply = false; // プレビュー先へ渡していない編集があるか
};
