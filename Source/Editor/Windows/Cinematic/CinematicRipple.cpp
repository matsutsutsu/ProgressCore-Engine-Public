#include "Editor/Windows/Cinematic/CinematicRipple.h"

#include "Engine/Cinematic/CinematicScript.h"

#include <algorithm>

namespace
{
    // 「後ろ」とみなす余裕（秒）。3.8 + 2.4 と 6.2 のような足し算の誤差で外れないようにする
    constexpr float kEdgeTolerance = 0.001f;



    // items のうち、時刻（member）が threshold 以降のものを、添字と時刻の組で out へ足す。skipIndex の項目は除く
    template <class Item, class Member>
    void CollectFrom(const std::vector<Item> &items, Member member, float threshold, std::size_t skipIndex,
                     CinematicRipple::TimeList &out)
    {
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (i == skipIndex) continue;
            const float time = items[i].*member;
            if (time >= threshold) out.emplace_back(i, time);
        }
    }



    // 控えた項目の時刻（member）を、元の時刻 + shift に書き直す。0 秒より前には出さない
    template <class Item, class Member>
    void PlaceShifted(std::vector<Item> &items, Member member, const CinematicRipple::TimeList &originals, float shift)
    {
        for (const auto &[index, originTime] : originals)
        {
            if (index < items.size()) items[index].*member = std::max(0.0f, originTime + shift);
        }
    }
}



CinematicRipple::Snapshot CinematicRipple::Capture(const CinematicScript &script, std::size_t shotIndex)
{
    Snapshot snapshot;
    if (shotIndex >= script.shots.size()) return snapshot;

    const CameraShot &shot  = script.shots[shotIndex];
    snapshot.valid          = true;
    snapshot.shotIndex      = shotIndex;
    snapshot.originEnd      = shot.start + shot.duration;
    snapshot.originDuration = script.duration;

    // ショット自身は除く。ほかの種類は除く添字がないので、どの添字とも重ならない配列の大きさを渡す
    const float threshold = snapshot.originEnd - kEdgeTolerance;
    CollectFrom(script.shots, &CameraShot::start, threshold, shotIndex, snapshot.shots);
    CollectFrom(script.sounds, &CinematicSound::time, threshold, script.sounds.size(), snapshot.sounds);
    CollectFrom(script.effects, &CinematicEffect::time, threshold, script.effects.size(), snapshot.effects);
    CollectFrom(script.signals, &CinematicSignal::time, threshold, script.signals.size(), snapshot.signals);
    CollectFrom(script.animations, &CinematicAnimation::start, threshold, script.animations.size(),
                snapshot.animations);
    return snapshot;
}



void CinematicRipple::Apply(CinematicScript &script, const Snapshot &snapshot)
{
    if (!snapshot.valid || snapshot.shotIndex >= script.shots.size()) return;

    const CameraShot &shot  = script.shots[snapshot.shotIndex];
    const float       shift = (shot.start + shot.duration) - snapshot.originEnd;

    PlaceShifted(script.shots, &CameraShot::start, snapshot.shots, shift);
    PlaceShifted(script.sounds, &CinematicSound::time, snapshot.sounds, shift);
    PlaceShifted(script.effects, &CinematicEffect::time, snapshot.effects, shift);
    PlaceShifted(script.signals, &CinematicSignal::time, snapshot.signals, shift);
    PlaceShifted(script.animations, &CinematicAnimation::start, snapshot.animations, shift);

    // 後ろの項目が全体の長さの外へ押し出されないよう、全体の長さも同じだけずらす
    script.duration = std::max(0.0f, snapshot.originDuration + shift);
}
