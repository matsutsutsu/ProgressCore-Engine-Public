#include "Engine/Cinematic/Systems/CinematicTrackSystem.h"
#include "Engine/Cinematic/CinematicEvents.h"
#include "Engine/Cinematic/CinematicSubject.h"
#include "Engine/Animation/Core/AnimEventMessages.h"
#include "Engine/Audio/IAudioAPI.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <algorithm>
#include <memory>
#include <vector>

using namespace CCL::ECS;

namespace
{
    // 前の再生の台本が差し替わった後に離すときのクロスフェード時間（秒）
    // 台本が替わると、握っていたアニメの blendOut はもう引けない
    constexpr float kFallbackReleaseBlend = 0.3f;
} // namespace

// ---------------------------------------------------------------------------
// 毎ステップの入口
// ---------------------------------------------------------------------------

void CinematicTrackSystem::Update(float /*dt*/)
{
    // 処理フロー:
    // 1. 新しい再生に入ったら、前の再生の続きを片付けて頭から追う（途中から流すなら、開始位置まで飛ばす）
    // 2. 再生位置を過ぎた点（SE・エフェクト・シグナル）を集める
    // 3. アニメの区間を出たものを離し、入ったものを握る
    // 4. 集めた点を出す

    FireList fires;

    ForEach([&](CinematicCameraComponent &cine) {
        // ---------------------------------------------------------
        // 1. 新しい再生
        // ---------------------------------------------------------
        if (cine.trackPlayCount != cine.playCount)
        {
            // 前のムービーが終わったのを見る前に次の再生が始まると、握ったままのアニメが残る
            for (EntityID held : cine.animHeld)
            {
                if (held != InvalidEntityID) ReleaseAnimation(held, kFallbackReleaseBlend);
            }

            cine.nextSound  = 0;
            cine.nextEffect = 0;
            cine.nextSignal = 0;
            cine.animHeld.assign(cine.script.animations.size(), InvalidEntityID);
            cine.animEntered.assign(cine.script.animations.size(), 0);
            cine.trackPlayCount = cine.playCount;

            // 途中から流すとき（エディタの「ここから再生」）は、開始位置より前の点を出さずに飛ばす
            // 開始位置ちょうどの点は出す。区間が開始位置までに終わっているアニメは握らない
            const CinematicScript &s    = cine.script;
            const float            from = cine.playStartTime;
            while (cine.nextSound < s.sounds.size() && s.sounds[cine.nextSound].time < from) ++cine.nextSound;
            while (cine.nextEffect < s.effects.size() && s.effects[cine.nextEffect].time < from) ++cine.nextEffect;
            while (cine.nextSignal < s.signals.size() && s.signals[cine.nextSignal].time < from) ++cine.nextSignal;
            for (std::size_t i = 0; i < s.animations.size(); ++i)
            {
                if (s.animations[i].End() <= from) cine.animEntered[i] = 1;
            }
        }

        // ---------------------------------------------------------
        // 2. 点
        // ---------------------------------------------------------
        CollectPoints(cine, fires);

        // ---------------------------------------------------------
        // 3. アニメ
        // ---------------------------------------------------------
        SyncAnimations(cine);
    });

    // ---------------------------------------------------------
    // 4. 点を出す
    // ---------------------------------------------------------

    // ForEach を抜けてから出す
    // エフェクトの受け手は、受けたその場で Entity を作るので、Component の走査と重ねない
    for (const auto &fire : fires) fire();
}



// ---------------------------------------------------------------------------
// 点（SE・エフェクト・シグナル）
// ---------------------------------------------------------------------------

void CinematicTrackSystem::CollectPoints(CinematicCameraComponent &cine, FireList &out) const
{
    // 処理フロー:
    // 1. 出してよい状態かを決める（スキップで終えたら、出さずに飛ばす）
    // 2. SE を集める
    // 3. エフェクトを集める
    // 4. シグナルを集める

    const CinematicScript &script = cine.script;

    // ---------------------------------------------------------
    // 1. 出してよい状態
    // ---------------------------------------------------------

    // スキップで飛ばした分は出さない
    // カメラがもう映していない場面の音やエフェクトだけが出るのは不自然なため
    if (cine.state == CinematicState::Finished && cine.skipped)
    {
        cine.nextSound  = script.sounds.size();
        cine.nextEffect = script.effects.size();
        cine.nextSignal = script.signals.size();
        return;
    }

    // 開始待ちの間は再生位置が 0 のままなので、0 秒の点も動き出してから出す
    // 最後まで再生して終えたときは、終わりちょうどの点まで出す
    const bool firing = (cine.state == CinematicState::Playing) || (cine.state == CinematicState::Finished);
    if (!firing) return;

    // ---------------------------------------------------------
    // 2. SE
    // ---------------------------------------------------------

    // 次に出す番号から順に、時刻を過ぎたものを集める
    // 時刻の昇順に並んでいるので、過ぎていないものに当たったら、それより後はまだ出さない
    while (cine.nextSound < script.sounds.size() && script.sounds[cine.nextSound].time <= cine.time)
    {
        const CinematicSound &sound = script.sounds[cine.nextSound++];

        // 対象がなければ、距離で減衰しない 2D で鳴らす
        // ムービーはカメラが対象から遠いことが多く、3D だと小さくしか聞こえないため
        if (sound.subject.empty())
        {
            out.push_back([this, hash = CCL::Utils::HashString(sound.event.c_str()), volume = sound.volume,
                           pitch = sound.pitch]() {
                if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;
                auto &api = m_world->GetResource<std::shared_ptr<IAudioAPI>>();
                if (api) api->PlayOneShot(hash, volume, pitch);
            });
            continue;
        }

        const EntityID entity = Cinematic::FindEntityByName(*m_world, sound.subject);
        if (entity == InvalidEntityID)
        {
            CCL_LOG_WARN(LogCategory::Game, "[Cinematic] SE の対象 '%s' が見つからないので鳴らしません: %s",
                         sound.subject.c_str(), sound.event.c_str());
            continue;
        }
        out.push_back([this, msg = AnimEventSoundMessage{entity, sound.event, sound.volume, sound.pitch}]() {
            m_world->GetEventBus().Publish(msg);
        });
    }

    // ---------------------------------------------------------
    // 3. エフェクト
    // ---------------------------------------------------------
    while (cine.nextEffect < script.effects.size() && script.effects[cine.nextEffect].time <= cine.time)
    {
        const CinematicEffect &effect = script.effects[cine.nextEffect++];

        const EntityID entity = Cinematic::FindEntityByName(*m_world, effect.subject);
        if (entity == InvalidEntityID)
        {
            CCL_LOG_WARN(LogCategory::Game, "[Cinematic] エフェクトの対象 '%s' が見つからないので出しません: %s",
                         effect.subject.c_str(), effect.effect.c_str());
            continue;
        }

        // アニメの Effect イベントと同じ経路で出す
        // 向き・大きさ・追従・継続放出の止め方まで、アニメのシーケンサーと同じ扱いになる
        out.push_back([this, msg = AnimEventEffectMessage{entity, effect.effect, effect.offset, effect.scale,
                                                          effect.follow, effect.duration, ""}]() {
            m_world->GetEventBus().Publish(msg);
        });
    }

    // ---------------------------------------------------------
    // 4. シグナル
    // ---------------------------------------------------------

    // シグナルの意味はエンジンでは決めない。名前と引数をそのまま渡し、受け手が解釈する
    while (cine.nextSignal < script.signals.size() && script.signals[cine.nextSignal].time <= cine.time)
    {
        const CinematicSignal &signal = script.signals[cine.nextSignal++];
        out.push_back([this, ev = CinematicSignalEvent{signal.name, signal.param, signal.param2, signal.value}]() {
            m_world->GetEventBus().Publish(ev);
        });
    }
}



// ---------------------------------------------------------------------------
// アニメ
// ---------------------------------------------------------------------------

void CinematicTrackSystem::SyncAnimations(CinematicCameraComponent &cine) const
{
    // 処理フロー:
    // 1. 区間を出たもの（再生中でなくなったものを含む）を、手放す候補にする
    // 2. 区間に入ったものを握る
    // 3. 候補のうち、別のクリップがもう握っていない Entity だけを離す

    const std::vector<CinematicAnimation> &clips   = cine.script.animations;
    const bool                             playing = (cine.state == CinematicState::Playing);

    // 握っている Entity の列は、新しい再生に入ったとき（Update の 1）に台本のアニメの数へそろえている
    // 数が合わないときは添字の対応が崩れているので、範囲外を読まないよう何もしない
    if (cine.animHeld.size() != clips.size() || cine.animEntered.size() != clips.size()) return;

    auto inRange = [&](const CinematicAnimation &c) { return playing && cine.time >= c.start && cine.time < c.End(); };

    // ---------------------------------------------------------
    // 1. 手放す候補
    // ---------------------------------------------------------

    // ここではまだ離さない。同じ相手を次のクリップが握っているかは、2 で握った後でないと分からない
    // 先に離すと、重ねて置いた次のクリップまで手放してステートのクリップへ戻ってしまう
    struct PendingRelease
    {
        EntityID entity;
        float    blendOut;
    };
    std::vector<PendingRelease> releases;

    for (std::size_t i = 0; i < clips.size(); ++i)
    {
        if (cine.animHeld[i] == InvalidEntityID || inRange(clips[i])) continue;
        releases.push_back({cine.animHeld[i], clips[i].blendOut});
        cine.animHeld[i] = InvalidEntityID;
    }

    // ---------------------------------------------------------
    // 2. 握る
    // ---------------------------------------------------------
    for (std::size_t i = 0; i < clips.size(); ++i)
    {
        if (cine.animEntered[i] || !inRange(clips[i])) continue;

        // 区間に入る処理は1回だけにする
        // 対象が見つからなかったときに、区間の間ずっと探し直して警告を出し続けないため
        cine.animEntered[i] = 1;

        const EntityID entity = Cinematic::FindEntityByName(*m_world, clips[i].subject);
        if (entity == InvalidEntityID)
        {
            CCL_LOG_WARN(LogCategory::Game, "[Cinematic] アニメの対象 '%s' が見つからないので再生しません: %s",
                         clips[i].subject.c_str(), clips[i].sequence.c_str());
            continue;
        }
        // 開始位置より前から始まっているアニメ（「ここから再生」で区間の途中から入った）は、
        // 区間の頭から今までの分だけ進めた位置から握る。頭から流すときは区間の頭で入るので 0
        const float clipOffset =
            (clips[i].start < cine.playStartTime) ? (cine.time - clips[i].start) * clips[i].speed : 0.0f;

        if (HoldAnimation(entity, clips[i], clipOffset)) cine.animHeld[i] = entity;
    }

    // ---------------------------------------------------------
    // 3. 離す
    // ---------------------------------------------------------

    // 別のクリップが握っている相手は離さない
    // 次のクリップは握った時点で、自分の blendIn で前のクリップから混ぜ始めている（重ねて置いたときも、ぴったりつなげたときも）
    // ここで離すと、そのクリップを止めてステートのクリップへ戻してしまう
    for (const PendingRelease &release : releases)
    {
        const bool stillHeld =
            std::find(cine.animHeld.begin(), cine.animHeld.end(), release.entity) != cine.animHeld.end();
        if (!stillHeld) ReleaseAnimation(release.entity, release.blendOut);
    }
}



bool CinematicTrackSystem::HoldAnimation(EntityID entity, const CinematicAnimation &clip, float clipOffset) const
{
    auto *animator = m_world->GetComponent<AnimatorComponent>(entity);
    if (!animator) return false;

    // ステートに入るときと同じく、同じクリップでも頭から再生し直す（理由は PlayParams::restart）
    animator->Play(clip.resolved, {.blendTime = clip.blendIn, .loop = clip.loop, .speed = clip.speed, .restart = true});
    animator->isExternalOverride = true;

    // 途中から握るときは、再生位置を進めておく
    // 飛ばした分のアニメのイベント（音・エフェクト）は出さないよう、イベントの番号も合わせる
    // 速度カーブのあるクリップは、等速とみなした近い位置になる
    if (clipOffset > 0.0f)
    {
        animator->currentTimer = clipOffset;
        animator->SyncEventCursor();
    }

    // 直前に離した要求が残っていれば取り消す。続けて握り直したときに、ステートへ戻されないため
    animator->externalReleaseBlend = -1.0f;
    return true;
}



void CinematicTrackSystem::ReleaseAnimation(EntityID entity, float blendOut) const
{
    if (!m_world->IsEntityValid(entity)) return;

    auto *animator = m_world->GetComponent<AnimatorComponent>(entity);
    if (!animator) return;

    // 戻すクリップは AnimControllerSystem が決める（握る前のステートのクリップ）
    animator->isExternalOverride   = false;
    animator->externalReleaseBlend = blendOut;
}



// 入力の受付と同じ段。アニメを握るのは、アニメのステートを進める AnimControllerSystem（L02）より前にする
REGISTER_LOGIC_SYSTEM(CinematicTrackSystem, Priority::LogicStage::L01_Input);
