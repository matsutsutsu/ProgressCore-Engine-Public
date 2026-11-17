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
    // 台本が差し替わった後に離すときのクロスフェード時間（秒）
    constexpr float kFallbackReleaseBlend = 0.3f;
}





void CinematicTrackSystem::Update(float dt)
{
    FireList fires;

    ForEach([&](CinematicCameraComponent &cine) {
        // 新しい再生に入ったら、前の再生で握ったアニメを離して最初から追い直す
        if (cine.trackPlayCount != cine.playCount) Restart(cine);

        // 再生位置を過ぎた SE・エフェクト・シグナルを集める（出すのは走査の後）
        CollectPoints(cine, fires);

        // アニメの区間に入ったクリップを握り、出たクリップを離す
        SyncAnimations(cine);
    });

    // エフェクトの受け手はその場で Entity を作るので、Component の走査を抜けてから出す
    for (const auto &fire : fires) fire();
}



void CinematicTrackSystem::Restart(CinematicCameraComponent &cine) const
{
    for (EntityID held : cine.animHeld)
    {
        if (held != InvalidEntityID) ReleaseAnimation(held, kFallbackReleaseBlend);
    }

    const CinematicScript &s = cine.script;
    cine.trackPlayCount      = cine.playCount;
    cine.nextSound           = 0;
    cine.nextEffect          = 0;
    cine.nextSignal          = 0;
    cine.animHeld.assign(s.animations.size(), InvalidEntityID);
    cine.animEntered.assign(s.animations.size(), 0);

    // 途中から流すときは、開始位置より前の点を出さない。開始位置ちょうどの点は出す
    const float from = cine.playStartTime;
    while (cine.nextSound < s.sounds.size() && s.sounds[cine.nextSound].time < from) ++cine.nextSound;
    while (cine.nextEffect < s.effects.size() && s.effects[cine.nextEffect].time < from) ++cine.nextEffect;
    while (cine.nextSignal < s.signals.size() && s.signals[cine.nextSignal].time < from) ++cine.nextSignal;

    for (std::size_t i = 0; i < s.animations.size(); ++i)
    {
        if (s.animations[i].End() <= from) cine.animEntered[i] = 1;
    }
}



void CinematicTrackSystem::CollectPoints(CinematicCameraComponent &cine, FireList &out) const
{
    // 処理フロー:
    // 1. 出してよい状態かを決める
    // 2. SE を集める
    // 3. エフェクトを集める
    // 4. シグナルを集める

    const CinematicScript &script = cine.script;

    // ---------------------------------------------------------
    // 1. 出してよい状態
    // ---------------------------------------------------------

    // スキップで飛ばした場面の音やエフェクトだけが出ると不自然なので、残りは出さない
    if (cine.state == CinematicState::Finished && cine.skipped)
    {
        cine.nextSound  = script.sounds.size();
        cine.nextEffect = script.effects.size();
        cine.nextSignal = script.signals.size();
        return;
    }

    // 最後まで再生して終えたときは、終わりちょうどの点まで出す
    if (cine.state == CinematicState::Idle) return;

    // ---------------------------------------------------------
    // 2. SE
    // ---------------------------------------------------------
    while (cine.nextSound < script.sounds.size() && script.sounds[cine.nextSound].time <= cine.time)
    {
        const CinematicSound &sound = script.sounds[cine.nextSound++];

        // 対象がなければ 2D で鳴らす。ムービーはカメラが対象から遠く、3D だと小さくしか聞こえない
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

        // アニメの Effect イベントと同じ経路で出すので、追従や継続放出の扱いもアニメと同じになる
        out.push_back([this, msg = AnimEventEffectMessage{entity, effect.effect, effect.offset, effect.scale,
                                                          effect.follow, effect.duration, ""}]() {
            m_world->GetEventBus().Publish(msg);
        });
    }

    // ---------------------------------------------------------
    // 4. シグナル
    // ---------------------------------------------------------
    while (cine.nextSignal < script.signals.size() && script.signals[cine.nextSignal].time <= cine.time)
    {
        const CinematicSignal &signal = script.signals[cine.nextSignal++];
        out.push_back([this, ev = CinematicSignalEvent{signal.name, signal.param, signal.param2, signal.value}]() {
            m_world->GetEventBus().Publish(ev);
        });
    }
}



void CinematicTrackSystem::SyncAnimations(CinematicCameraComponent &cine) const
{
    // 処理フロー:
    // 1. 区間を出たものを、手放す候補にする
    // 2. 区間に入ったものを握る
    // 3. 候補のうち、ほかのクリップが握っていない Entity だけを離す

    const std::vector<CinematicAnimation> &clips   = cine.script.animations;
    const bool                             playing = (cine.state == CinematicState::Playing);
    if (cine.animHeld.size() != clips.size() || cine.animEntered.size() != clips.size()) return;

    auto inRange = [&](const CinematicAnimation &c) { return playing && cine.time >= c.start && cine.time < c.End(); };

    // ---------------------------------------------------------
    // 1. 手放す候補
    // ---------------------------------------------------------

    // ここで離すと、同じ相手に重ねて置いた次のクリップまで外れてしまうので、握った後で離す
    std::vector<std::pair<EntityID, float>> releases; // （Entity, blendOut）
    for (std::size_t i = 0; i < clips.size(); ++i)
    {
        if (cine.animHeld[i] == InvalidEntityID || inRange(clips[i])) continue;
        releases.emplace_back(cine.animHeld[i], clips[i].blendOut);
        cine.animHeld[i] = InvalidEntityID;
    }

    // ---------------------------------------------------------
    // 2. 握る
    // ---------------------------------------------------------
    for (std::size_t i = 0; i < clips.size(); ++i)
    {
        if (cine.animEntered[i] || !inRange(clips[i])) continue;

        // 対象がいなくても、区間の間ずっと探し直して警告を出し続けないよう1回で済ませる
        cine.animEntered[i] = 1;

        const EntityID entity = Cinematic::FindEntityByName(*m_world, clips[i].subject);
        if (entity == InvalidEntityID)
        {
            CCL_LOG_WARN(LogCategory::Game, "[Cinematic] アニメの対象 '%s' が見つからないので再生しません: %s",
                         clips[i].subject.c_str(), clips[i].sequence.c_str());
            continue;
        }

        // 途中から流して区間の途中で入ったときは、その分進めた位置から握る
        const float clipOffset =
            (clips[i].start < cine.playStartTime) ? (cine.time - clips[i].start) * clips[i].speed : 0.0f;

        if (HoldAnimation(entity, clips[i], clipOffset)) cine.animHeld[i] = entity;
    }

    // ---------------------------------------------------------
    // 3. 離す
    // ---------------------------------------------------------
    for (const auto &[entity, blendOut] : releases)
    {
        const bool stillHeld = std::find(cine.animHeld.begin(), cine.animHeld.end(), entity) != cine.animHeld.end();
        if (!stillHeld) ReleaseAnimation(entity, blendOut);
    }
}



bool CinematicTrackSystem::HoldAnimation(EntityID entity, const CinematicAnimation &clip, float clipOffset) const
{
    auto *animator = m_world->GetComponent<AnimatorComponent>(entity);
    if (!animator) return false;

    animator->Play(clip.resolved, {.blendTime = clip.blendIn, .loop = clip.loop, .speed = clip.speed, .restart = true});
    animator->isExternalOverride = true;

    // 飛ばした分のアニメのイベント（音・エフェクト）は出さないよう、イベントの番号も合わせる
    if (clipOffset > 0.0f)
    {
        animator->currentTimer = clipOffset;
        animator->SyncEventCursor();
    }

    // 直前に離した要求が残っていると、握り直した直後にステートへ戻されてしまう
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



// アニメを握るのは、アニメのステートを進める AnimControllerSystem（L02）より前にする
REGISTER_LOGIC_SYSTEM(CinematicTrackSystem, Priority::LogicStage::L01_Input);
