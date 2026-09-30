//------------------------------------------------------------------------------
// VERSION 0.9.1
//
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# include "imgui_node_editor_internal.h"
# include <cstdio> 
# include <string>
# include <fstream>
# include <bitset>
# include <climits>
# include <algorithm>
# include <sstream>
# include <streambuf>
# include <type_traits>

namespace ed = ax::NodeEditor::Detail;


//------------------------------------------------------------------------------
//
// Animation
//
//------------------------------------------------------------------------------
ed::Animation::Animation(EditorContext* editor):
    Editor(editor),
    m_State(Stopped),
    m_Time(0.0f),
    m_Duration(0.0f)
{
}

ed::Animation::~Animation()
{
    Stop();
}

void ed::Animation::Play(float duration)
{
    if (IsPlaying())
        Stop();

    m_State = Playing;
    if (duration < 0)
        duration = 0.0f;

    m_Time     = 0.0f;
    m_Duration = duration;

    OnPlay();

    Editor->RegisterAnimation(this);

    if (duration == 0.0f)
        Finish();
}

void ed::Animation::Stop()
{
    if (!IsPlaying())
        return;

    m_State = Stopped;

    Editor->UnregisterAnimation(this);

    OnStop();
}

void ed::Animation::Finish()
{
    if (!IsPlaying())
        return;

    OnFinish();

    Stop();
}

void ed::Animation::Update()
{
    if (!IsPlaying())
        return;

    m_Time += ImMax(0.0f, ImGui::GetIO().DeltaTime);
    if (m_Time < m_Duration)
    {
        const float progress = GetProgress();
        OnUpdate(progress);
    }
    else
    {
        OnFinish();
        Stop();
    }
}




//------------------------------------------------------------------------------
//
// Navigate Animation
//
//------------------------------------------------------------------------------
ed::NavigateAnimation::NavigateAnimation(EditorContext* editor, NavigateAction& scrollAction):
    Animation(editor),
    Action(scrollAction)
{
}

void ed::NavigateAnimation::NavigateTo(const ImRect& target, float duration)
{
    Stop();

    m_Start      = Action.GetViewRect();
    m_Target     = target;

    // Skip tiny animations
    auto minoffset = m_Target.Min - m_Start.Min;
    auto maxOffset = m_Target.Max - m_Start.Max;
    auto epsilon   = 1e-4f;
    if (ImFabs(minoffset.x) < epsilon && ImFabs(minoffset.y) < epsilon &&
        ImFabs(maxOffset.x) < epsilon && ImFabs(maxOffset.y) < epsilon)
    {
        duration = 0;
    }

    Play(duration);
}

void ed::NavigateAnimation::OnUpdate(float progress)
{
    ImRect current;
    current.Min = ImEasing::EaseOutQuad(m_Start.Min, m_Target.Min - m_Start.Min, progress);
    current.Max = ImEasing::EaseOutQuad(m_Start.Max, m_Target.Max - m_Start.Max, progress);
    Action.SetViewRect(current);
}

void ed::NavigateAnimation::OnStop()
{
    Editor->MakeDirty(SaveReasonFlags::Navigation);
}

void ed::NavigateAnimation::OnFinish()
{
    Action.SetViewRect(m_Target);

    Editor->MakeDirty(SaveReasonFlags::Navigation);
}




//------------------------------------------------------------------------------
//
// Flow Animation
//
//------------------------------------------------------------------------------
ed::FlowAnimation::FlowAnimation(FlowAnimationController* controller):
    Animation(controller->Editor),
    Controller(controller),
    m_Link(nullptr),
    m_Offset(0.0f),
    m_PathLength(0.0f)
{
}

void ed::FlowAnimation::Flow(ed::Link* link, float markerDistance, float speed, float duration)
{
    Stop();

    if (m_Link != link)
    {
        m_Offset = 0.0f;
        ClearPath();
    }

    if (m_MarkerDistance != markerDistance)
        ClearPath();

    m_MarkerDistance = markerDistance;
    m_Speed          = speed;
    m_Link           = link;

    Play(duration);
}

void ed::FlowAnimation::Draw(ImDrawList* drawList)
{
    if (!IsPlaying() || !IsLinkValid() || !m_Link->IsVisible())
        return;

    if (!IsPathValid())
        UpdatePath();

    m_Offset = fmodf(m_Offset, m_MarkerDistance);
    if (m_Offset < 0)
        m_Offset += m_MarkerDistance;

    const auto progress    = GetProgress();

    const auto flowAlpha = 1.0f - progress * progress;
    const auto flowColor = Editor->GetColor(StyleColor_Flow, flowAlpha);
    //const auto flowPath  = Link->GetCurve();

    m_Link->Draw(drawList, flowColor, 2.0f);

    if (IsPathValid())
    {
        //Offset = 0;

        const auto markerAlpha  = powf(1.0f - progress, 0.35f);
        const auto markerRadius = 4.0f * (1.0f - progress) + 2.0f;
        const auto markerColor  = Editor->GetColor(StyleColor_FlowMarker, markerAlpha);

        for (float d = m_Offset; d < m_PathLength; d += m_MarkerDistance)
            drawList->AddCircleFilled(SamplePath(d), markerRadius, markerColor);
    }
}

bool ed::FlowAnimation::IsLinkValid() const
{
    return m_Link && m_Link->m_IsLive;
}

bool ed::FlowAnimation::IsPathValid() const
{
    return m_Path.size() > 1 && m_PathLength > 0.0f && m_Link->m_Start == m_LastStart && m_Link->m_End == m_LastEnd;
}

void ed::FlowAnimation::UpdatePath()
{
    if (!IsLinkValid())
    {
        ClearPath();
        return;
    }

    const auto curve  = m_Link->GetCurve();

    m_LastStart  = m_Link->m_Start;
    m_LastEnd    = m_Link->m_End;
    m_PathLength = ImCubicBezierLength(curve.P0, curve.P1, curve.P2, curve.P3);

    auto collectPointsCallback = [this](ImCubicBezierFixedStepSample& result)
    {
        m_Path.push_back(CurvePoint{ result.Length, result.Point });
    };

    const auto step = ImMax(m_MarkerDistance * 0.5f, 15.0f);

    m_Path.resize(0);
    ImCubicBezierFixedStep(collectPointsCallback, curve, step, false, 0.5f, 0.001f);
}

void ed::FlowAnimation::ClearPath()
{
    vector<CurvePoint>().swap(m_Path);
    m_PathLength = 0.0f;
}

ImVec2 ed::FlowAnimation::SamplePath(float distance) const
{
    //distance = ImMax(0.0f, std::min(distance, PathLength));

    auto endPointIt = std::find_if(m_Path.begin(), m_Path.end(), [distance](const CurvePoint& p) { return distance < p.Distance; });
    if (endPointIt == m_Path.end())
        endPointIt = m_Path.end() - 1;
    else if (endPointIt == m_Path.begin())
        endPointIt = m_Path.begin() + 1;

    const auto& start = endPointIt[-1];
    const auto& end   = *endPointIt;
    const auto  t     = (distance - start.Distance) / (end.Distance - start.Distance);

    return start.Point + (end.Point - start.Point) * t;
}

void ed::FlowAnimation::OnUpdate(float progress)
{
    IM_UNUSED(progress);

    m_Offset += m_Speed * ImGui::GetIO().DeltaTime;
}

void ed::FlowAnimation::OnStop()
{
    Controller->Release(this);
}




//------------------------------------------------------------------------------
//
// Flow Animation Controller
//
//------------------------------------------------------------------------------
ed::FlowAnimationController::FlowAnimationController(EditorContext* editor):
    AnimationController(editor)
{
}

ed::FlowAnimationController::~FlowAnimationController()
{
    for (auto animation : m_Animations)
        delete animation;
}

void ed::FlowAnimationController::Flow(Link* link, FlowDirection direction)
{
    if (!link || !link->m_IsLive)
        return;

    auto& editorStyle = GetStyle();

    auto animation = GetOrCreate(link);

    float speedDirection = 1.0f;
    if (direction == FlowDirection::Backward)
        speedDirection = -1.0f;

    animation->Flow(link, editorStyle.FlowMarkerDistance, editorStyle.FlowSpeed * speedDirection, editorStyle.FlowDuration);
}

void ed::FlowAnimationController::Draw(ImDrawList* drawList)
{
    if (m_Animations.empty())
        return;

    drawList->ChannelsSetCurrent(c_LinkChannel_Flow);

    for (auto animation : m_Animations)
        animation->Draw(drawList);
}

ed::FlowAnimation* ed::FlowAnimationController::GetOrCreate(Link* link)
{
    // Return live animation which match target link
    {
        auto animationIt = std::find_if(m_Animations.begin(), m_Animations.end(), [link](FlowAnimation* animation) { return animation->m_Link == link; });
        if (animationIt != m_Animations.end())
            return *animationIt;
    }

    // There are no live animations for target link, try to reuse inactive old one
    if (!m_FreePool.empty())
    {
        auto animation = m_FreePool.back();
        m_FreePool.pop_back();
        return animation;
    }

    // Cache miss, allocate new one
    auto animation = new FlowAnimation(this);
    m_Animations.push_back(animation);

    return animation;
}

void ed::FlowAnimationController::Release(FlowAnimation* animation)
{
    IM_UNUSED(animation);
}


