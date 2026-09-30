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
# include <cstdio> // snprintf
# include <cmath>  // std::isfinite
# include <string>
# include <fstream>
# include <bitset>
# include <climits>
# include <algorithm>
# include <sstream>
# include <streambuf>
# include <type_traits>

// https://stackoverflow.com/a/8597498
# define DECLARE_HAS_NESTED(Name, Member)                                          \
                                                                                   \
    template<class T>                                                              \
    struct has_nested_ ## Name                                                     \
    {                                                                              \
        typedef char yes;                                                          \
        typedef yes(&no)[2];                                                       \
                                                                                   \
        template<class U> static yes test(decltype(U::Member)*);                   \
        template<class U> static no  test(...);                                    \
                                                                                   \
        static bool const value = sizeof(test<T>(0)) == sizeof(yes);               \
    };


namespace ax {
namespace NodeEditor {
namespace Detail {

# if !defined(IMGUI_VERSION_NUM) || (IMGUI_VERSION_NUM < 18822)
# define DECLARE_KEY_TESTER(Key)                                                                    \
    DECLARE_HAS_NESTED(Key, Key)                                                                    \
    struct KeyTester_ ## Key                                                                        \
    {                                                                                               \
        template <typename T>                                                                       \
        static int Get(typename std::enable_if<has_nested_ ## Key<ImGuiKey_>::value, T>::type*)     \
        {                                                                                           \
            return ImGui::GetKeyIndex(T::Key);                                                      \
        }                                                                                           \
                                                                                                    \
        template <typename T>                                                                       \
        static int Get(typename std::enable_if<!has_nested_ ## Key<ImGuiKey_>::value, T>::type*)    \
        {                                                                                           \
            return -1;                                                                              \
        }                                                                                           \
    }

DECLARE_KEY_TESTER(ImGuiKey_F);
DECLARE_KEY_TESTER(ImGuiKey_D);

static inline int GetKeyIndexForF()
{
    return KeyTester_ImGuiKey_F::Get<ImGuiKey_>(nullptr);
}

static inline int GetKeyIndexForD()
{
    return KeyTester_ImGuiKey_D::Get<ImGuiKey_>(nullptr);
}
# else
static inline ImGuiKey GetKeyIndexForF()
{
    return ImGuiKey_F;
}

static inline ImGuiKey GetKeyIndexForD()
{
    return ImGuiKey_D;
}
# endif

} // namespace Detail
} // namespace NodeEditor
} // namespace ax


//------------------------------------------------------------------------------
namespace ax {
namespace NodeEditor {
namespace Detail {

namespace ed = ax::NodeEditor::Detail;


//------------------------------------------------------------------------------
static const int c_BackgroundChannelCount = 1;
const int c_LinkChannelCount       = 4;
static const int c_UserLayersCount        = 5;

static const int c_UserLayerChannelStart  = 0;
static const int c_BackgroundChannelStart = c_UserLayerChannelStart  + c_UserLayersCount;
const int c_LinkStartChannel       = c_BackgroundChannelStart + c_BackgroundChannelCount;
const int c_NodeStartChannel       = c_LinkStartChannel       + c_LinkChannelCount;

const int c_BackgroundChannel_SelectionRect = c_BackgroundChannelStart + 0;

const int c_UserChannel_Content         = c_UserLayerChannelStart + 1;
const int c_UserChannel_Grid            = c_UserLayerChannelStart + 2;
const int c_UserChannel_HintsBackground = c_UserLayerChannelStart + 3;
const int c_UserChannel_Hints           = c_UserLayerChannelStart + 4;

const int c_LinkChannel_Selection  = c_LinkStartChannel + 0;
const int c_LinkChannel_Links      = c_LinkStartChannel + 1;
const int c_LinkChannel_Flow       = c_LinkStartChannel + 2;
const int c_LinkChannel_NewLink    = c_LinkStartChannel + 3;

const int c_ChannelsPerNode           = 5;
static const int c_NodeBaseChannel           = 0;
static const int c_NodeBackgroundChannel     = 1;
static const int c_NodeUserBackgroundChannel = 2;
static const int c_NodePinChannel            = 3;
static const int c_NodeContentChannel        = 4;

const float c_GroupSelectThickness       = 6.0f;  // canvas pixels
const float c_LinkSelectThickness        = 5.0f;  // canvas pixels
static const float c_NavigationZoomMargin       = 0.1f;  // percentage of visible bounds
static const float c_MouseZoomDuration          = 0.15f; // seconds
static const float c_SelectionFadeOutDuration   = 0.15f; // seconds

static const auto  c_MaxMoveOverEdgeSpeed       = 10.0f;
static const auto  c_MaxMoveOverEdgeDistance    = 300.0f;

#if IMGUI_VERSION_NUM > 18101
static const auto  c_AllRoundCornersFlags = ImDrawFlags_RoundCornersAll;
#else
static const auto  c_AllRoundCornersFlags = 15;
#endif


//------------------------------------------------------------------------------
# if defined(_DEBUG) && defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* string);
# endif

static void LogV(const char* fmt, va_list args)
{
    const int buffer_size = 1024;
    static char buffer[1024];

    vsnprintf(buffer, buffer_size - 1, fmt, args);
    buffer[buffer_size - 1] = 0;

    // Always log to stdout/console with immediate flush
    fprintf(stdout, "NodeEditor: %s\n", buffer);
    fflush(stdout);
    
    // Also try stderr
    fprintf(stderr, "NodeEditor: %s\n", buffer);
    fflush(stderr);
    
    

# if defined(_DEBUG) && defined(_WIN32)
    // Also log to Visual Studio debug output
    OutputDebugStringA("NodeEditor: ");
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
# endif
}

void ed::Log(const char* fmt, ...)
{
    
}


//------------------------------------------------------------------------------
bool IsGroup(const ed::Node* node)
{
    if (node && node->m_Type == ed::NodeType::Group)
        return true;
    else
        return false;
}


//------------------------------------------------------------------------------
static void ImDrawListSplitter_Grow(ImDrawList* draw_list, ImDrawListSplitter* splitter, int channels_count)
{
    IM_ASSERT(splitter != nullptr);
    IM_ASSERT(splitter->_Count <= channels_count);

    if (splitter->_Count == 1)
    {
        splitter->Split(draw_list, channels_count);
        return;
    }

    int old_channels_count = splitter->_Channels.Size;
    if (old_channels_count < channels_count)
    {
        splitter->_Channels.reserve(channels_count);
        splitter->_Channels.resize(channels_count);
    }
    int old_used_channels_count = splitter->_Count;
    splitter->_Count = channels_count;

    for (int i = old_used_channels_count; i < channels_count; i++)
    {
        if (i >= old_channels_count)
        {
            IM_PLACEMENT_NEW(&splitter->_Channels[i]) ImDrawChannel();
        }
        else
        {
            splitter->_Channels[i]._CmdBuffer.resize(0);
            splitter->_Channels[i]._IdxBuffer.resize(0);
        }
    }
}

void ImDrawList_ChannelsGrow(ImDrawList* draw_list, int channels_count)
{
    ImDrawListSplitter_Grow(draw_list, &draw_list->_Splitter, channels_count);
}

static void ImDrawListSplitter_SwapChannels(ImDrawListSplitter* splitter, int left, int right)
{
    IM_ASSERT(left < splitter->_Count && right < splitter->_Count);
    if (left == right)
        return;

    auto currentChannel = splitter->_Current;

    auto* leftCmdBuffer  = &splitter->_Channels[left]._CmdBuffer;
    auto* leftIdxBuffer  = &splitter->_Channels[left]._IdxBuffer;
    auto* rightCmdBuffer = &splitter->_Channels[right]._CmdBuffer;
    auto* rightIdxBuffer = &splitter->_Channels[right]._IdxBuffer;

    leftCmdBuffer->swap(*rightCmdBuffer);
    leftIdxBuffer->swap(*rightIdxBuffer);

    if (currentChannel == left)
        splitter->_Current = right;
    else if (currentChannel == right)
        splitter->_Current = left;
}

void ImDrawList_SwapChannels(ImDrawList* drawList, int left, int right)
{
    ImDrawListSplitter_SwapChannels(&drawList->_Splitter, left, right);
}

void ImDrawList_SwapSplitter(ImDrawList* drawList, ImDrawListSplitter& splitter)
{
    auto& currentSplitter = drawList->_Splitter;

    std::swap(currentSplitter._Current, splitter._Current);
    std::swap(currentSplitter._Count, splitter._Count);
    currentSplitter._Channels.swap(splitter._Channels);
}

//static void ImDrawList_TransformChannel_Inner(ImVector<ImDrawVert>& vtxBuffer, const ImVector<ImDrawIdx>& idxBuffer, const ImVector<ImDrawCmd>& cmdBuffer, const ImVec2& preOffset, const ImVec2& scale, const ImVec2& postOffset)
//{
//    auto idxRead = idxBuffer.Data;
//
//    int indexOffset = 0;
//    for (auto& cmd : cmdBuffer)
//    {
//        auto idxCount = cmd.ElemCount;
//
//        if (idxCount == 0) continue;
//
//        auto minIndex = idxRead[indexOffset];
//        auto maxIndex = idxRead[indexOffset];
//
//        for (auto i = 1u; i < idxCount; ++i)
//        {
//            auto idx = idxRead[indexOffset + i];
//            minIndex = std::min(minIndex, idx);
//            maxIndex = ImMax(maxIndex, idx);
//        }
//
//        for (auto vtx = vtxBuffer.Data + minIndex, vtxEnd = vtxBuffer.Data + maxIndex + 1; vtx < vtxEnd; ++vtx)
//        {
//            vtx->pos.x = (vtx->pos.x + preOffset.x) * scale.x + postOffset.x;
//            vtx->pos.y = (vtx->pos.y + preOffset.y) * scale.y + postOffset.y;
//        }
//
//        indexOffset += idxCount;
//    }
//}

//static void ImDrawList_TransformChannels(ImDrawList* drawList, int begin, int end, const ImVec2& preOffset, const ImVec2& scale, const ImVec2& postOffset)
//{
//    int lastCurrentChannel = drawList->_ChannelsCurrent;
//    if (lastCurrentChannel != 0)
//        drawList->ChannelsSetCurrent(0);
//
//    auto& vtxBuffer = drawList->VtxBuffer;
//
//    if (begin == 0 && begin != end)
//    {
//        ImDrawList_TransformChannel_Inner(vtxBuffer, drawList->IdxBuffer, drawList->CmdBuffer, preOffset, scale, postOffset);
//        ++begin;
//    }
//
//    for (int channelIndex = begin; channelIndex < end; ++channelIndex)
//    {
//        auto& channel = drawList->_Channels[channelIndex];
//        ImDrawList_TransformChannel_Inner(vtxBuffer, channel.IdxBuffer, channel.CmdBuffer, preOffset, scale, postOffset);
//    }
//
//    if (lastCurrentChannel != 0)
//        drawList->ChannelsSetCurrent(lastCurrentChannel);
//}

//static void ImDrawList_ClampClipRects_Inner(ImVector<ImDrawCmd>& cmdBuffer, const ImVec4& clipRect, const ImVec2& offset)
//{
//    for (auto& cmd : cmdBuffer)
//    {
//        cmd.ClipRect.x = ImMax(cmd.ClipRect.x + offset.x, clipRect.x);
//        cmd.ClipRect.y = ImMax(cmd.ClipRect.y + offset.y, clipRect.y);
//        cmd.ClipRect.z = std::min(cmd.ClipRect.z + offset.x, clipRect.z);
//        cmd.ClipRect.w = std::min(cmd.ClipRect.w + offset.y, clipRect.w);
//    }
//}

//static void ImDrawList_TranslateAndClampClipRects(ImDrawList* drawList, int begin, int end, const ImVec2& offset)
//{
//    int lastCurrentChannel = drawList->_ChannelsCurrent;
//    if (lastCurrentChannel != 0)
//        drawList->ChannelsSetCurrent(0);
//
//    auto clipRect = drawList->_ClipRectStack.back();
//
//    if (begin == 0 && begin != end)
//    {
//        ImDrawList_ClampClipRects_Inner(drawList->CmdBuffer, clipRect, offset);
//        ++begin;
//    }
//
//    for (int channelIndex = begin; channelIndex < end; ++channelIndex)
//    {
//        auto& channel = drawList->_Channels[channelIndex];
//        ImDrawList_ClampClipRects_Inner(channel.CmdBuffer, clipRect, offset);
//    }
//
//    if (lastCurrentChannel != 0)
//        drawList->ChannelsSetCurrent(lastCurrentChannel);
//}

static void ImDrawList_PathBezierOffset(ImDrawList* drawList, float offset, const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3)
{
    using namespace ed;

    auto acceptPoint = [drawList, offset](const ImCubicBezierSubdivideSample& r)
    {
        drawList->PathLineTo(r.Point + ImNormalized(ImVec2(-r.Tangent.y, r.Tangent.x)) * offset);
    };

    ImCubicBezierSubdivide(acceptPoint, p0, p1, p2, p3);
}

static void ImDrawList_AddBezierWithArrows(ImDrawList* drawList, const ImCubicBezierPoints& curve, float thickness,
    float startArrowSize, float startArrowWidth, float endArrowSize, float endArrowWidth,
    bool fill, ImU32 color, float strokeThickness, const ImVec2* startDirHint = nullptr, const ImVec2* endDirHint = nullptr)
{

//    printf("[CHECKPOINT] ImDrawList_AddBezierWithArrows: thickness=%f\n", thickness);
//    fflush(stdout);

    using namespace ax;

    if ((color >> 24) == 0)
        return;

    const auto half_thickness = thickness * 5.0f;

    if (fill)
    {
        drawList->AddBezierCubic(curve.P0, curve.P1, curve.P2, curve.P3, color, thickness);

        if (startArrowSize > 0.0f)
        {
            const auto start_dir  = ImNormalized(startDirHint ? *startDirHint : ImCubicBezierTangent(curve.P0, curve.P1, curve.P2, curve.P3, 0.0f));
            const auto start_n    = ImVec2(-start_dir.y, start_dir.x);
            const auto half_width = startArrowWidth * 0.5f;
            const auto tip        = curve.P0 - start_dir * startArrowSize;

            drawList->PathLineTo(curve.P0 - start_n * ImMax(half_width, half_thickness));
            drawList->PathLineTo(curve.P0 + start_n * ImMax(half_width, half_thickness));
            drawList->PathLineTo(tip);
            drawList->PathFillConvex(color);
        }

        if (endArrowSize > 0.0f)
        {
            const auto    end_dir = ImNormalized(endDirHint ? -*endDirHint : ImCubicBezierTangent(curve.P0, curve.P1, curve.P2, curve.P3, 1.0f));
            const auto    end_n   = ImVec2(  -end_dir.y,   end_dir.x);
            const auto half_width = endArrowWidth * 0.5f;
            const auto tip        = curve.P3 + end_dir * endArrowSize;

            drawList->PathLineTo(curve.P3 + end_n * ImMax(half_width, half_thickness));
            drawList->PathLineTo(curve.P3 - end_n * ImMax(half_width, half_thickness));
            drawList->PathLineTo(tip);
            drawList->PathFillConvex(color);
        }
    }
    else
    {
        if (startArrowSize > 0.0f)
        {
            const auto start_dir  = ImNormalized(ImCubicBezierTangent(curve.P0, curve.P1, curve.P2, curve.P3, 0.0f));
            const auto start_n    = ImVec2(-start_dir.y, start_dir.x);
            const auto half_width = startArrowWidth * 0.5f;
            const auto tip        = curve.P0 - start_dir * startArrowSize;

            if (half_width > half_thickness)
                drawList->PathLineTo(curve.P0 - start_n * half_width);
            drawList->PathLineTo(tip);
            if (half_width > half_thickness)
                drawList->PathLineTo(curve.P0 + start_n * half_width);
        }

        ImDrawList_PathBezierOffset(drawList, half_thickness, curve.P0, curve.P1, curve.P2, curve.P3);

        if (endArrowSize > 0.0f)
        {
            const auto    end_dir = ImNormalized(ImCubicBezierTangent(curve.P0, curve.P1, curve.P2, curve.P3, 1.0f));
            const auto    end_n   = ImVec2(  -end_dir.y,   end_dir.x);
            const auto half_width = endArrowWidth * 0.5f;
            const auto tip        = curve.P3 + end_dir * endArrowSize;

            if (half_width > half_thickness)
                drawList->PathLineTo(curve.P3 + end_n * half_width);
            drawList->PathLineTo(tip);
            if (half_width > half_thickness)
                drawList->PathLineTo(curve.P3 - end_n * half_width);
        }

        ImDrawList_PathBezierOffset(drawList, half_thickness, curve.P3, curve.P2, curve.P1, curve.P0);

        drawList->PathStroke(color, true, strokeThickness);
    }
}




//------------------------------------------------------------------------------
//
// Pin
//
//------------------------------------------------------------------------------
void ed::Pin::Draw(ImDrawList* drawList, DrawFlags flags)
{
    if (flags & Hovered)
    {
        drawList->ChannelsSetCurrent(m_Node->m_Channel + c_NodePinChannel);

        drawList->AddRectFilled(m_Bounds.Min, m_Bounds.Max,
            m_Color, m_Rounding, m_Corners);

        if (m_BorderWidth > 0.0f)
        {
            FringeScaleScope fringe(1.0f);
            drawList->AddRect(m_Bounds.Min, m_Bounds.Max,
                m_BorderColor, m_Rounding, m_Corners, m_BorderWidth);
        }

        if (!Editor->IsSelected(m_Node))
            m_Node->Draw(drawList, flags);
    }
}

ImVec2 ed::Pin::GetClosestPoint(const ImVec2& p) const
{
    auto pivot  = m_Pivot;
    auto extent = m_Radius + m_ArrowSize;

    if (m_SnapLinkToDir && extent > 0.0f)
    {
        pivot.Min += m_Dir * extent;
        pivot.Max += m_Dir * extent;

        extent = 0;
    }

    return ImRect_ClosestPoint(pivot, p, true, extent);
}

ImLine ed::Pin::GetClosestLine(const Pin* pin) const
{
    auto pivotA  =      m_Pivot;
    auto pivotB  = pin->m_Pivot;
    auto extentA =      m_Radius +      m_ArrowSize;
    auto extentB = pin->m_Radius + pin->m_ArrowSize;

    if (m_SnapLinkToDir && extentA > 0.0f)
    {
        pivotA.Min += m_Dir * extentA;
        pivotA.Max += m_Dir * extentA;

        extentA = 0;
    }

    if (pin->m_SnapLinkToDir && extentB > 0.0f)
    {
        pivotB.Min += pin->m_Dir * extentB;
        pivotB.Max += pin->m_Dir * extentB;

        extentB = 0;
    }

    return ImRect_ClosestLine(pivotA, pivotB, extentA, extentB);
}




//------------------------------------------------------------------------------
//
// Node
//
//------------------------------------------------------------------------------
bool ed::Node::AcceptDrag()
{
    m_DragStart = m_Bounds.Min;
    return true;
}

void ed::Node::UpdateDrag(const ImVec2& offset)
{
    auto size = m_Bounds.GetSize();
    m_Bounds.Min = ImFloor(m_DragStart + offset);
    m_Bounds.Max = m_Bounds.Min + size;
}

bool ed::Node::EndDrag()
{
    return m_Bounds.Min != m_DragStart;
}

void ed::Node::Draw(ImDrawList* drawList, DrawFlags flags)
{
    if (flags == Detail::Object::None)
    {
        drawList->ChannelsSetCurrent(m_Channel + c_NodeBackgroundChannel);

        drawList->AddRectFilled(
            m_Bounds.Min,
            m_Bounds.Max,
            m_Color, m_Rounding);

        if (IsGroup(this))
        {
            drawList->AddRectFilled(
                m_GroupBounds.Min,
                m_GroupBounds.Max,
                m_GroupColor, m_GroupRounding);

            if (m_GroupBorderWidth > 0.0f)
            {
                FringeScaleScope fringe(1.0f);

                drawList->AddRect(
                    m_GroupBounds.Min,
                    m_GroupBounds.Max,
                    m_GroupBorderColor, m_GroupRounding, c_AllRoundCornersFlags, m_GroupBorderWidth);
            }
        }

# if 0
        // #debug: highlight group regions
        auto drawRect = [drawList](const ImRect& rect, ImU32 color)
        {
            if (ImRect_IsEmpty(rect)) return;
            drawList->AddRectFilled(rect.Min, rect.Max, color);
        };

        drawRect(GetRegionBounds(NodeRegion::Top), IM_COL32(255, 0, 0, 64));
        drawRect(GetRegionBounds(NodeRegion::Bottom), IM_COL32(255, 0, 0, 64));
        drawRect(GetRegionBounds(NodeRegion::Left), IM_COL32(0, 255, 0, 64));
        drawRect(GetRegionBounds(NodeRegion::Right), IM_COL32(0, 255, 0, 64));
        drawRect(GetRegionBounds(NodeRegion::TopLeft), IM_COL32(255, 0, 255, 64));
        drawRect(GetRegionBounds(NodeRegion::TopRight), IM_COL32(255, 0, 255, 64));
        drawRect(GetRegionBounds(NodeRegion::BottomLeft), IM_COL32(255, 0, 255, 64));
        drawRect(GetRegionBounds(NodeRegion::BottomRight), IM_COL32(255, 0, 255, 64));
        drawRect(GetRegionBounds(NodeRegion::Center), IM_COL32(0, 0, 255, 64));
        drawRect(GetRegionBounds(NodeRegion::Header), IM_COL32(0, 255, 255, 64));
# endif

        DrawBorder(drawList, m_BorderColor, m_BorderWidth);
    }
    else if (flags & Selected)
    {
        const auto  borderColor = Editor->GetColor(StyleColor_SelNodeBorder);
        const auto& editorStyle = Editor->GetStyle();

        drawList->ChannelsSetCurrent(m_Channel + c_NodeBaseChannel);

        DrawBorder(drawList, borderColor, editorStyle.SelectedNodeBorderWidth, editorStyle.SelectedNodeBorderOffset);
    }
    else if (!IsGroup(this) && (flags & Hovered))
    {
        const auto  borderColor = Editor->GetColor(StyleColor_HovNodeBorder);
        const auto& editorStyle = Editor->GetStyle();

        drawList->ChannelsSetCurrent(m_Channel + c_NodeBaseChannel);

        DrawBorder(drawList, borderColor, editorStyle.HoveredNodeBorderWidth, editorStyle.HoverNodeBorderOffset);
    }
}

void ed::Node::DrawBorder(ImDrawList* drawList, ImU32 color, float thickness, float offset)
{
    if (thickness > 0.0f)
    {
        const ImVec2 extraOffset = ImVec2(offset, offset);

        drawList->AddRect(m_Bounds.Min - extraOffset, m_Bounds.Max + extraOffset,
            color, ImMax(0.0f, m_Rounding + offset), c_AllRoundCornersFlags, thickness);
    }
}

void ed::Node::GetGroupedNodes(std::vector<Node*>& result, bool append)
{
    if (!append)
        result.resize(0);

    if (!IsGroup(this))
        return;

    const auto firstNodeIndex = result.size();
    Editor->FindNodesInRect(m_GroupBounds, result, true, false);

    for (auto index = firstNodeIndex; index < result.size(); ++index)
        result[index]->GetGroupedNodes(result, true);
}

ImRect ed::Node::GetRegionBounds(NodeRegion region) const
{
    if (m_Type == NodeType::Node)
    {
        if (region == NodeRegion::Header)
            return m_Bounds;
    }
    else if (m_Type == NodeType::Group)
    {
        const float activeAreaMinimumSize = ImMax(ImMax(
            Editor->GetView().InvScale * c_GroupSelectThickness,
            m_GroupBorderWidth), c_GroupSelectThickness);
        const float minimumSize = activeAreaMinimumSize * 5;

        auto bounds = m_Bounds;
        if (bounds.GetWidth() < minimumSize)
            bounds.Expand(ImVec2(minimumSize - bounds.GetWidth(), 0.0f));
        if (bounds.GetHeight() < minimumSize)
            bounds.Expand(ImVec2(0.0f, minimumSize - bounds.GetHeight()));

        if (region == NodeRegion::Top)
        {
            bounds.Max.y = bounds.Min.y + activeAreaMinimumSize;
            bounds.Min.x += activeAreaMinimumSize;
            bounds.Max.x -= activeAreaMinimumSize;
            return bounds;
        }
        else if (region == NodeRegion::Bottom)
        {
            bounds.Min.y = bounds.Max.y - activeAreaMinimumSize;
            bounds.Min.x += activeAreaMinimumSize;
            bounds.Max.x -= activeAreaMinimumSize;
            return bounds;
        }
        else if (region == NodeRegion::Left)
        {
            bounds.Max.x = bounds.Min.x + activeAreaMinimumSize;
            bounds.Min.y += activeAreaMinimumSize;
            bounds.Max.y -= activeAreaMinimumSize;
            return bounds;
        }
        else if (region == NodeRegion::Right)
        {
            bounds.Min.x = bounds.Max.x - activeAreaMinimumSize;
            bounds.Min.y += activeAreaMinimumSize;
            bounds.Max.y -= activeAreaMinimumSize;
            return bounds;
        }
        else if (region == NodeRegion::TopLeft)
        {
            bounds.Max.x = bounds.Min.x + activeAreaMinimumSize * 2;
            bounds.Max.y = bounds.Min.y + activeAreaMinimumSize * 2;
            return bounds;
        }
        else if (region == NodeRegion::TopRight)
        {
            bounds.Min.x = bounds.Max.x - activeAreaMinimumSize * 2;
            bounds.Max.y = bounds.Min.y + activeAreaMinimumSize * 2;
            return bounds;
        }
        else if (region == NodeRegion::BottomRight)
        {
            bounds.Min.x = bounds.Max.x - activeAreaMinimumSize * 2;
            bounds.Min.y = bounds.Max.y - activeAreaMinimumSize * 2;
            return bounds;
        }
        else if (region == NodeRegion::BottomLeft)
        {
            bounds.Max.x = bounds.Min.x + activeAreaMinimumSize * 2;
            bounds.Min.y = bounds.Max.y - activeAreaMinimumSize * 2;
            return bounds;
        }
        else if (region == NodeRegion::Header)
        {
            bounds.Min.x += activeAreaMinimumSize;
            bounds.Max.x -= activeAreaMinimumSize;
            bounds.Min.y += activeAreaMinimumSize;
            bounds.Max.y  = ImMax(bounds.Min.y + activeAreaMinimumSize, m_GroupBounds.Min.y);
            return bounds;
        }
        else if (region == NodeRegion::Center)
        {
            bounds.Max.x -= activeAreaMinimumSize;
            bounds.Min.y  = ImMax(bounds.Min.y + activeAreaMinimumSize, m_GroupBounds.Min.y);
            bounds.Min.x += activeAreaMinimumSize;
            bounds.Max.y -= activeAreaMinimumSize;
            return bounds;
        }
    }

    return ImRect();
}

ed::NodeRegion ed::Node::GetRegion(const ImVec2& point) const
{
    if (m_Type == NodeType::Node)
    {
        if (m_Bounds.Contains(point))
            return NodeRegion::Header;
        else
            return NodeRegion::None;
    }
    else if (m_Type == NodeType::Group)
    {
        static const NodeRegion c_Regions[] =
        {
            // Corners first, they may overlap other regions.
            NodeRegion::TopLeft,
            NodeRegion::TopRight,
            NodeRegion::BottomLeft,
            NodeRegion::BottomRight,
            NodeRegion::Header,
            NodeRegion::Top,
            NodeRegion::Bottom,
            NodeRegion::Left,
            NodeRegion::Right,
            NodeRegion::Center
        };

        for (auto region : c_Regions)
        {
            auto bounds = GetRegionBounds(region);
            if (bounds.Contains(point))
                return region;
        }
    }

    return NodeRegion::None;
}




//------------------------------------------------------------------------------
//
// Link
//
//------------------------------------------------------------------------------
void ed::Link::Draw(ImDrawList* drawList, DrawFlags flags)
{
    if (flags == None)
    {
        drawList->ChannelsSetCurrent(c_LinkChannel_Links);

        Draw(drawList, m_Color, 0.0f);
    }
    else if (flags & Selected)
    {
        const auto borderColor = Editor->GetColor(StyleColor_SelLinkBorder);

        drawList->ChannelsSetCurrent(c_LinkChannel_Selection);

        Draw(drawList, borderColor, 2.0f);  // Reduced from 4.5f for thinner outline
    }
    else if (flags & Hovered)
    {
        const auto borderColor = Editor->GetColor(StyleColor_HovLinkBorder);

        drawList->ChannelsSetCurrent(c_LinkChannel_Selection);

        Draw(drawList, borderColor, 2.0f);
    }
    else if (flags & Highlighted)
    {
        drawList->ChannelsSetCurrent(c_LinkChannel_Selection);

        Draw(drawList, m_HighlightColor, 3.5f);
    }
}

void ed::Link::Draw(ImDrawList* drawList, ImU32 color, float extraThickness) const
{
    if (!m_IsLive)
        return;
    
    // Check if this is a straight link
    if (m_GuidedData && m_GuidedData->Mode == LinkMode::Straight)
    {
        // Draw simple straight line
        drawList->AddLine(m_Start, m_End, color, m_Thickness + extraThickness);
        
        // Draw start arrow
        if (m_StartPin && m_StartPin->m_ArrowSize > 0.0f)
        {
            const auto dir = ImNormalized(m_End - m_Start);
            const auto n = ImVec2(-dir.y, dir.x);
            const auto arrowSize = m_StartPin->m_ArrowSize + extraThickness;
            const auto arrowWidth = m_StartPin->m_ArrowWidth + extraThickness;
            const auto halfWidth = arrowWidth * 0.5f;
            const auto tip = m_Start - dir * arrowSize;
            
            drawList->AddTriangleFilled(
                m_Start + n * halfWidth,
                m_Start - n * halfWidth,
                tip,
                color);
        }
        
        // Draw end arrow
        if (m_EndPin && m_EndPin->m_ArrowSize > 0.0f)
        {
            const auto dir = ImNormalized(m_End - m_Start);
            const auto n = ImVec2(-dir.y, dir.x);
            const auto arrowSize = m_EndPin->m_ArrowSize + extraThickness;
            const auto arrowWidth = m_EndPin->m_ArrowWidth + extraThickness;
            const auto halfWidth = arrowWidth * 0.5f;
            const auto tip = m_End + dir * arrowSize;
            
            drawList->AddTriangleFilled(
                m_End - n * halfWidth,
                m_End + n * halfWidth,
                tip,
                color);
        }
    }
    // Check if this is a guided link with control points
    else if (m_GuidedData && m_GuidedData->Mode == LinkMode::Guided && !m_GuidedData->ControlPoints.empty())
    {
        // Draw as linear segments between waypoints
        // Filter out invalid control points during drawing
        const float MAX_VALID_COORD = 1000000.0f;
        std::vector<ImVec2> points;
        points.push_back(m_Start);
        for (const auto& cp : m_GuidedData->ControlPoints)
        {
            // Only add valid control points
            if (std::isfinite(cp.Position.x) && std::isfinite(cp.Position.y) &&
                std::abs(cp.Position.x) < MAX_VALID_COORD && std::abs(cp.Position.y) < MAX_VALID_COORD)
            {
                points.push_back(cp.Position);
            }
        }
        points.push_back(m_End);
        
        // Draw each line segment
        for (size_t i = 0; i < points.size() - 1; ++i)
        {
            const ImVec2& p0 = points[i];
            const ImVec2& p1 = points[i + 1];
            
            // Draw the line
            drawList->AddLine(p0, p1, color, m_Thickness + extraThickness);
            
            // Draw arrows on first and last segments
            if (i == 0 && m_StartPin && m_StartPin->m_ArrowSize > 0.0f)
            {
                const auto dir = ImNormalized(p1 - p0);
                const auto n = ImVec2(-dir.y, dir.x);
                const auto arrowSize = m_StartPin->m_ArrowSize + extraThickness;
                const auto arrowWidth = m_StartPin->m_ArrowWidth + extraThickness;
                const auto halfWidth = arrowWidth * 0.5f;
                const auto tip = p0 - dir * arrowSize;
                
                drawList->AddTriangleFilled(
                    p0 + n * halfWidth,
                    p0 - n * halfWidth,
                    tip,
                    color);
            }
            
            if (i == points.size() - 2 && m_EndPin && m_EndPin->m_ArrowSize > 0.0f)
            {
                const auto dir = ImNormalized(p1 - p0);
                const auto n = ImVec2(-dir.y, dir.x);
                const auto arrowSize = m_EndPin->m_ArrowSize + extraThickness;
                const auto arrowWidth = m_EndPin->m_ArrowWidth + extraThickness;
                const auto halfWidth = arrowWidth * 0.5f;
                const auto tip = p1 + dir * arrowSize;
                
                drawList->AddTriangleFilled(
                    p1 - n * halfWidth,
                    p1 + n * halfWidth,
                    tip,
                    color);
            }
        }
        
        // Always draw control points when in guided mode
        DrawControlPoints(drawList);
    }
    else
    {
        // Original single-curve drawing
        const auto curve = GetCurve();

        ImDrawList_AddBezierWithArrows(drawList, curve, m_Thickness + extraThickness,
            m_StartPin && m_StartPin->m_ArrowSize  > 0.0f ? m_StartPin->m_ArrowSize  + extraThickness : 0.0f,
            m_StartPin && m_StartPin->m_ArrowWidth > 0.0f ? m_StartPin->m_ArrowWidth + extraThickness : 0.0f,
              m_EndPin &&   m_EndPin->m_ArrowSize  > 0.0f ?   m_EndPin->m_ArrowSize  + extraThickness : 0.0f,
              m_EndPin &&   m_EndPin->m_ArrowWidth > 0.0f ?   m_EndPin->m_ArrowWidth + extraThickness : 0.0f,
            true, color, 1.0f,
            m_StartPin && m_StartPin->m_SnapLinkToDir ? &m_StartPin->m_Dir : nullptr,
            m_EndPin   &&   m_EndPin->m_SnapLinkToDir ?   &m_EndPin->m_Dir : nullptr);
    }
}

void ed::Link::UpdateEndpoints()
{
    const auto line = m_StartPin->GetClosestLine(m_EndPin);
    m_Start = line.A;
    m_End   = line.B;
}

std::vector<ImCubicBezierPoints> ed::Link::GetCurveSegments() const
{
    std::vector<ImCubicBezierPoints> segments;
    
    // Check if this is a guided link
    if (m_GuidedData && m_GuidedData->Mode == LinkMode::Guided && !m_GuidedData->ControlPoints.empty())
    {
        // Use guided link curve generation
        auto easeLinkStrength = [](const ImVec2& a, const ImVec2& b, float strength)
        {
            const auto distanceX    = b.x - a.x;
            const auto distanceY    = b.y - a.y;
            const auto distance     = ImSqrt(distanceX * distanceX + distanceY * distanceY);
            const auto halfDistance = distance * 0.5f;

            if (halfDistance < strength)
                strength = strength * ImSin(IM_PI * 0.5f * halfDistance / strength);

            return strength;
        };

        const auto startStrength = easeLinkStrength(m_Start, m_End, m_StartPin->m_Strength);
        const auto   endStrength = easeLinkStrength(m_Start, m_End,   m_EndPin->m_Strength);
        
        segments = m_GuidedData->GetCurveSegments(
            m_Start, m_End,
            m_StartPin->m_Dir, m_EndPin->m_Dir,
            startStrength, endStrength);
    }
    
    if (segments.empty())
    {
        // Fall back to single automatic curve
        segments.push_back(GetCurve());
    }
    
    return segments;
}

ImCubicBezierPoints ed::Link::GetCurve() const
{
    // If guided with control points, return first segment for backward compatibility
    if (m_GuidedData && m_GuidedData->Mode == LinkMode::Guided && !m_GuidedData->ControlPoints.empty())
    {
        auto segments = GetCurveSegments();
        return segments.empty() ? ImCubicBezierPoints() : segments[0];
    }
    
    // Original automatic calculation
    auto easeLinkStrength = [](const ImVec2& a, const ImVec2& b, float strength)
    {
        const auto distanceX    = b.x - a.x;
        const auto distanceY    = b.y - a.y;
        const auto distance     = ImSqrt(distanceX * distanceX + distanceY * distanceY);
        const auto halfDistance = distance * 0.5f;

        if (halfDistance < strength)
            strength = strength * ImSin(IM_PI * 0.5f * halfDistance / strength);

        return strength;
    };

    const auto startStrength = easeLinkStrength(m_Start, m_End, m_StartPin->m_Strength);
    const auto   endStrength = easeLinkStrength(m_Start, m_End,   m_EndPin->m_Strength);
    
    // Adjust pin directions for smoother curves when pins are misaligned
    const auto delta = m_End - m_Start;
    const float horizontalDistance = ImFabs(delta.x);
    const float verticalDistance = ImFabs(delta.y);
    
    ImVec2 startDir = m_StartPin->m_Dir;
    ImVec2 endDir = m_EndPin->m_Dir;
    
    // Check if pins have primarily vertical or horizontal orientations
    const bool startIsVertical = ImFabs(startDir.y) > ImFabs(startDir.x);
    const bool endIsVertical = ImFabs(endDir.y) > ImFabs(endDir.x);
    const bool startIsHorizontal = ImFabs(startDir.x) > ImFabs(startDir.y);
    const bool endIsHorizontal = ImFabs(endDir.x) > ImFabs(endDir.y);
    
    // Horizontally aligned pins with vertical directions: blend towards horizontal
    if (verticalDistance < horizontalDistance * 0.25f && startIsVertical && endIsVertical)
    {
        const float horizontalSign = delta.x >= 0.0f ? 1.0f : -1.0f;
        const float blendFactor = ImMin(1.0f, horizontalDistance / (startStrength + endStrength));
        
        startDir = ImNormalized(ImVec2(
            startDir.x + horizontalSign * blendFactor * 2.0f,
            startDir.y * (1.0f - blendFactor * 0.5f)
        ));
        
        endDir = ImNormalized(ImVec2(
            endDir.x - horizontalSign * blendFactor * 2.0f,
            endDir.y * (1.0f - blendFactor * 0.5f)
        ));
    }
    // Vertically aligned pins with horizontal directions: blend towards vertical
    else if (horizontalDistance < verticalDistance * 0.25f && startIsHorizontal && endIsHorizontal)
    {
        const float verticalSign = delta.y >= 0.0f ? 1.0f : -1.0f;
        const float blendFactor = ImMin(1.0f, verticalDistance / (startStrength + endStrength));
        
        startDir = ImNormalized(ImVec2(
            startDir.x * (1.0f - blendFactor * 0.5f),
            startDir.y + verticalSign * blendFactor * 2.0f
        ));
        
        endDir = ImNormalized(ImVec2(
            endDir.x * (1.0f - blendFactor * 0.5f),
            endDir.y - verticalSign * blendFactor * 2.0f
        ));
    }
    
    const auto cp0 = m_Start + startDir * startStrength;
    const auto cp1 =   m_End + endDir   * endStrength;

    ImCubicBezierPoints result;
    result.P0 = m_Start;
    result.P1 = cp0;
    result.P2 = cp1;
    result.P3 = m_End;

    return result;
}

int ed::Link::TestHitControlPoint(const ImVec2& point, float threshold) const
{
    if (!m_GuidedData || m_GuidedData->Mode != LinkMode::Guided)
        return -1;
    
    int result = m_GuidedData->FindControlPoint(point, threshold);
    
    if (m_GuidedData->ControlPoints.size() > 0)
    {
        for (int i = 0; i < static_cast<int>(m_GuidedData->ControlPoints.size()); ++i)
        {
            const auto& cp = m_GuidedData->ControlPoints[i];
            const ImVec2 delta = cp.Position - point;
            const float dist = sqrtf(delta.x * delta.x + delta.y * delta.y);
        }
    }
    
    return result;
}

void ed::Link::DrawControlPoints(ImDrawList* drawList) const
{
    if (!m_GuidedData || m_GuidedData->Mode != LinkMode::Guided)
        return;
    
    // Control point size scales with zoom for better visibility
    auto& style = Editor->GetStyle();
    const float scale = 1.0f / Editor->GetView().Scale;  // Scale inverse to zoom
    const float pointRadius = style.WaypointRadius * scale;
    ImU32 borderColor = style.WaypointBorderColor;
    
    for (int i = 0; i < static_cast<int>(m_GuidedData->ControlPoints.size()); ++i)
    {
        const auto& cp = m_GuidedData->ControlPoints[i];
        // Use canvas coordinates directly - the canvas system handles transformation
        const ImVec2& pos = cp.Position;
        
        // Determine if this control point is being dragged
        bool isDragging = (Editor->m_DraggingControlPointIndex >= 0 && 
                           Editor->m_DraggingControlPointIndex == i);
        // TODO: Add hover detection for WaypointColorHovered
        
        ImU32 cpColor = isDragging ? style.WaypointColorSelected : style.WaypointColor;
        
        // Draw control point circle
        drawList->AddCircleFilled(pos, pointRadius, cpColor);
        drawList->AddCircle(pos, pointRadius, borderColor, 0, style.WaypointBorderWidth);
    }
}

bool ed::Link::TestHit(const ImVec2& point, float extraThickness) const
{
    if (!m_IsLive)
        return false;

    auto bounds = GetBounds();
    if (extraThickness > 0.0f)
        bounds.Expand(extraThickness);

    if (!bounds.Contains(point))
        return false;
    
    // Test straight link (simple line)
    if (m_GuidedData && m_GuidedData->Mode == LinkMode::Straight)
    {
        const ImVec2 delta = m_End - m_Start;
        const float lengthSq = delta.x * delta.x + delta.y * delta.y;
        
        if (lengthSq > 0.0001f)
        {
            const ImVec2 pointDelta = point - m_Start;
            float t = (pointDelta.x * delta.x + pointDelta.y * delta.y) / lengthSq;
            t = ImClamp(t, 0.0f, 1.0f);
            
            const ImVec2 closest = m_Start + delta * t;
            const ImVec2 diff = point - closest;
            const float distSq = diff.x * diff.x + diff.y * diff.y;
            const float threshold = m_Thickness + extraThickness;
            
            if (distSq <= threshold * threshold)
                return true;
        }
        return false;
    }
    
    // Test control points first if guided
    if (m_GuidedData && m_GuidedData->Mode == LinkMode::Guided)
    {
        if (TestHitControlPoint(point, 10.0f) >= 0)
            return true;
    }
    
    // Test linear segments for guided links
    if (m_GuidedData && m_GuidedData->Mode == LinkMode::Guided && !m_GuidedData->ControlPoints.empty())
    {
        std::vector<ImVec2> points;
        points.push_back(m_Start);
        for (const auto& cp : m_GuidedData->ControlPoints)
            points.push_back(cp.Position);
        points.push_back(m_End);
        
        // Test each line segment
        for (size_t i = 0; i < points.size() - 1; ++i)
        {
            const ImVec2& p0 = points[i];
            const ImVec2& p1 = points[i + 1];
            
            // Distance from point to line segment
            const ImVec2 delta = p1 - p0;
            const float lengthSq = delta.x * delta.x + delta.y * delta.y;
            
            if (lengthSq < 0.0001f)
                continue;
            
            const ImVec2 pointDelta = point - p0;
            float t = (pointDelta.x * delta.x + pointDelta.y * delta.y) / lengthSq;
            t = ImClamp(t, 0.0f, 1.0f);
            
            const ImVec2 closest = p0 + delta * t;
            const ImVec2 diff = point - closest;
            const float distSq = diff.x * diff.x + diff.y * diff.y;
            const float threshold = m_Thickness + extraThickness;
            
            if (distSq <= threshold * threshold)
                return true;
        }
        return false;
    }

    // Original single-curve hit testing
    const auto bezier = GetCurve();
    const auto result = ImProjectOnCubicBezier(point, bezier.P0, bezier.P1, bezier.P2, bezier.P3, 50);

    return result.Distance <= m_Thickness + extraThickness;
}

bool ed::Link::TestHit(const ImRect& rect, bool allowIntersect) const
{
    if (!m_IsLive)
        return false;

    const auto bounds = GetBounds();

    if (rect.Contains(bounds))
        return true;

    if (!allowIntersect || !rect.Overlaps(bounds))
        return false;
    
    const auto p0 = rect.GetTL();
    const auto p1 = rect.GetTR();
    const auto p2 = rect.GetBR();
    const auto p3 = rect.GetBL();
    
    // Test all segments for guided links
    if (m_GuidedData && m_GuidedData->Mode == LinkMode::Guided && !m_GuidedData->ControlPoints.empty())
    {
        auto segments = GetCurveSegments();
        for (const auto& bezier : segments)
        {
            if (ImCubicBezierLineIntersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p0, p1).Count > 0)
                return true;
            if (ImCubicBezierLineIntersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p1, p2).Count > 0)
                return true;
            if (ImCubicBezierLineIntersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p2, p3).Count > 0)
                return true;
            if (ImCubicBezierLineIntersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p3, p0).Count > 0)
                return true;
        }
        return false;
    }

    // Original single-curve intersection test
    const auto bezier = GetCurve();

    if (ImCubicBezierLineIntersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p0, p1).Count > 0)
        return true;
    if (ImCubicBezierLineIntersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p1, p2).Count > 0)
        return true;
    if (ImCubicBezierLineIntersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p2, p3).Count > 0)
        return true;
    if (ImCubicBezierLineIntersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p3, p0).Count > 0)
        return true;

    return false;
}

ImRect ed::Link::GetBounds() const
{
    if (m_IsLive)
    {
        ImRect bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
        
        // For guided links with linear segments
        if (m_GuidedData && m_GuidedData->Mode == LinkMode::Guided && !m_GuidedData->ControlPoints.empty())
        {
            // Add all waypoints to bounds (filter out invalid control points)
            const float MAX_VALID_COORD = 1000000.0f;
            bounds.Add(m_Start);
            for (const auto& cp : m_GuidedData->ControlPoints)
            {
                // Skip invalid control points that would break bounds calculation
                if (std::isfinite(cp.Position.x) && std::isfinite(cp.Position.y) &&
                    std::abs(cp.Position.x) < MAX_VALID_COORD && std::abs(cp.Position.y) < MAX_VALID_COORD)
                {
                    bounds.Add(cp.Position);
                }
            }
            bounds.Add(m_End);
        }
        else
        {
            // Original bezier bounds
            const auto curve = GetCurve();
            bounds = ImCubicBezierBoundingRect(curve.P0, curve.P1, curve.P2, curve.P3);
        }

        if (bounds.GetWidth() == 0.0f)
        {
            bounds.Min.x -= 0.5f;
            bounds.Max.x += 0.5f;
        }

        if (bounds.GetHeight() == 0.0f)
        {
            bounds.Min.y -= 0.5f;
            bounds.Max.y += 0.5f;
        }

        // Add arrow bounds (approximation)
        if (m_StartPin->m_ArrowSize > 0.0f)
            bounds.Expand(m_StartPin->m_ArrowSize);
        if (m_EndPin->m_ArrowSize > 0.0f)
            bounds.Expand(m_EndPin->m_ArrowSize);

        return bounds;
    }
    else
        return ImRect();
}


//------------------------------------------------------------------------------
//
// Navigate Action
//
//------------------------------------------------------------------------------
const float ed::NavigateAction::s_DefaultZoomLevels[] =
{
    0.1f, 0.15f, 0.20f, 0.25f, 0.33f, 0.5f, 0.75f, 1.0f, 1.25f, 1.50f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f
};

const int ed::NavigateAction::s_DefaultZoomLevelCount = sizeof(s_DefaultZoomLevels) / sizeof(*s_DefaultZoomLevels);

ed::NavigateAction::NavigateAction(EditorContext* editor, ImGuiEx::Canvas& canvas):
    EditorAction(editor),
    m_IsActive(false),
    m_Zoom(1),
    m_VisibleRect(),
    m_Scroll(0, 0),
    m_ScrollStart(0, 0),
    m_ScrollDelta(0, 0),
    m_Canvas(canvas),
    m_WindowScreenPos(0, 0),
    m_WindowScreenSize(0, 0),
    m_Animation(editor, *this),
    m_Reason(NavigationReason::Unknown),
    m_LastSelectionId(0),
    m_LastObject(nullptr),
    m_MovingOverEdge(false),
    m_MoveScreenOffset(0, 0),
    m_ZoomLevels(editor->GetConfig().CustomZoomLevels.Size > 0 ? editor->GetConfig().CustomZoomLevels.Data : s_DefaultZoomLevels),
    m_ZoomLevelCount(editor->GetConfig().CustomZoomLevels.Size > 0 ? editor->GetConfig().CustomZoomLevels.Size : s_DefaultZoomLevelCount)
{
}

ed::EditorAction::AcceptResult ed::NavigateAction::Accept(const Control& control)
{
    IM_ASSERT(!m_IsActive);

    if (m_IsActive)
        return False;

    if (Editor->CanAcceptUserInput() /*&& !ImGui::IsAnyItemActive()*/ && ImGui::IsMouseDragging(Editor->GetConfig().NavigateButtonIndex, 0.0f))
    {
        m_IsActive    = true;
        m_ScrollStart = m_Scroll;
        m_ScrollDelta = ImGui::GetMouseDragDelta(Editor->GetConfig().NavigateButtonIndex);
        m_Scroll      = m_ScrollStart - m_ScrollDelta * m_Zoom;
    }

    auto& io = ImGui::GetIO();

    if (Editor->CanAcceptUserInput() && ImGui::IsKeyPressed(GetKeyIndexForF()) && Editor->AreShortcutsEnabled())
    {
        const auto zoomMode = io.KeyShift ? NavigateAction::ZoomMode::WithMargin : NavigateAction::ZoomMode::None;

        auto findHotObjectToZoom = [this, &control, &io]() -> Object*
        {
            if (control.HotObject)
            {
                if (auto pin = control.HotObject->AsPin())
                    return pin->m_Node;
                else
                    return control.HotObject;
            }
            else if (control.BackgroundHot)
            {
                auto node = Editor->FindNodeAt(io.MousePos);
                if (IsGroup(node))
                    return node;
            }

            return nullptr;
        };

        bool navigateToContent = false;
        if (!Editor->GetSelectedObjects().empty())
        {
            if (m_Reason != NavigationReason::Selection || m_LastSelectionId != Editor->GetSelectionId() || (zoomMode != NavigateAction::ZoomMode::None))
            {
                m_LastSelectionId = Editor->GetSelectionId();
                NavigateTo(Editor->GetSelectionBounds(), zoomMode, -1.0f, NavigationReason::Selection);
            }
            else
                navigateToContent = true;
        }
        else if(auto hotObject = findHotObjectToZoom())
        {
            if (m_Reason != NavigationReason::Object || m_LastObject != hotObject || (zoomMode != NavigateAction::ZoomMode::None))
            {
                m_LastObject = hotObject;
                auto bounds = hotObject->GetBounds();
                NavigateTo(bounds, zoomMode, -1.0f, NavigationReason::Object);
            }
            else
                navigateToContent = true;
        }
        else
            navigateToContent = true;

        if (navigateToContent)
            NavigateTo(Editor->GetContentBounds(), NavigateAction::ZoomMode::WithMargin, -1.0f, NavigationReason::Content);
    }

    auto visibleRect = GetViewRect();
    if (m_VisibleRect.Min != visibleRect.Min || m_VisibleRect.Max != visibleRect.Max)
    {
        m_VisibleRect = visibleRect;
        Editor->MakeDirty(SaveReasonFlags::Navigation);
    }

    // // #debug
    // if (m_DrawList)
    //     m_DrawList->AddCircleFilled(io.MousePos, 4.0f, IM_COL32(255, 0, 255, 255));

    if (HandleZoom(control))
        return True;

    return m_IsActive ? True : False;
}

bool ed::NavigateAction::Process(const Control& control)
{
    IM_UNUSED(control);

    if (!m_IsActive)
        return false;

    if (ImGui::IsMouseDragging(Editor->GetConfig().NavigateButtonIndex, 0.0f))
    {
        m_ScrollDelta = ImGui::GetMouseDragDelta(Editor->GetConfig().NavigateButtonIndex);
        m_Scroll      = m_ScrollStart - m_ScrollDelta * m_Zoom;
        m_VisibleRect = GetViewRect();
//         if (IsActive && Animation.IsPlaying())
//             Animation.Target = Animation.Target - ScrollDelta * Animation.TargetZoom;
    }
    else
    {
        if (m_Scroll != m_ScrollStart)
            Editor->MakeDirty(SaveReasonFlags::Navigation);

        m_IsActive = false;
    }

    // #TODO: Handle zoom while scrolling
    // HandleZoom(control);

    return m_IsActive;
}

bool ed::NavigateAction::HandleZoom(const Control& control)
{
    IM_UNUSED(control);

    const auto currentAction  = Editor->GetCurrentAction();
    const auto allowOffscreen = currentAction && currentAction->IsDragging();

    auto& io = ImGui::GetIO();

    if (!io.MouseWheel || (!allowOffscreen && !Editor->IsHoveredWithoutOverlapp()))// && !ImGui::IsAnyItemActive())
        return false;

    auto savedScroll = m_Scroll;
    auto savedZoom   = m_Zoom;

    m_Animation.Finish();

    auto mousePos = io.MousePos;
    auto newZoom  = GetNextZoom(io.MouseWheel);

    auto oldView   = GetView();
    m_Zoom = newZoom;
    auto newView   = GetView();

    auto screenPos = m_Canvas.FromLocal(mousePos, oldView);
    auto canvasPos = m_Canvas.ToLocal(screenPos, newView);

    auto offset       = (canvasPos - mousePos) * m_Zoom;
    auto targetScroll = m_Scroll - offset;

    auto visibleRect = GetViewRect();

    if (m_Scroll != savedScroll || m_Zoom != savedZoom || m_VisibleRect.Min != visibleRect.Min || m_VisibleRect.Max != visibleRect.Max)
    {
        m_Scroll      = savedScroll;
        m_Zoom        = savedZoom;
        m_VisibleRect = visibleRect;

        Editor->MakeDirty(SaveReasonFlags::Navigation);
    }

    auto targetRect = m_Canvas.CalcViewRect(ImGuiEx::CanvasView(-targetScroll, newZoom));

    NavigateTo(targetRect, c_MouseZoomDuration, NavigationReason::MouseZoom);

    return true;
}

void ed::NavigateAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", m_IsActive ? "yes" : "no");
    ImGui::Text("    Scroll: { x=%g y=%g }", m_Scroll.x, m_Scroll.y);
    ImGui::Text("    Zoom: %g", m_Zoom);
    ImGui::Text("    Visible Rect: { l=%g t=%g, r=%g b=%g w=%g h=%g }",
        m_VisibleRect.Min.x, m_VisibleRect.Min.y,
        m_VisibleRect.Max.x, m_VisibleRect.Max.y,
        m_VisibleRect.Max.x - m_VisibleRect.Min.x,
        m_VisibleRect.Max.y - m_VisibleRect.Min.y);
}

void ed::NavigateAction::NavigateTo(const ImRect& bounds, ZoomMode zoomMode, float duration, NavigationReason reason)
{
    if (ImRect_IsEmpty(bounds))
        return;

    if (duration < 0.0f)
        duration = GetStyle().ScrollDuration;

    if (zoomMode == ZoomMode::None)
    {
        auto viewRect       = m_Canvas.ViewRect();
        auto viewRectCenter = viewRect.GetCenter();
        auto targetCenter   = bounds.GetCenter();

        viewRect.Translate(targetCenter - viewRectCenter);

        NavigateTo(viewRect, duration, reason);
    }
    else
    {
        // Grow rect by 5% to leave some reasonable margin
        // from the edges of the canvas.
        auto rect   = bounds;

        if (zoomMode == ZoomMode::WithMargin)
        {
            auto extend = ImMax(rect.GetWidth(), rect.GetHeight());
            rect.Expand(extend * c_NavigationZoomMargin * 0.5f);
        }

        NavigateTo(rect, duration, reason);
    }
}

void ed::NavigateAction::NavigateTo(const ImRect& target, float duration, NavigationReason reason)
{
    m_Reason = reason;

    m_Animation.NavigateTo(target, duration);
}

void ed::NavigateAction::StopNavigation()
{
    m_Animation.Stop();
}

void ed::NavigateAction::FinishNavigation()
{
    m_Animation.Finish();
}

bool ed::NavigateAction::MoveOverEdge(const ImVec2& canvasSize)
{
    // Don't interrupt non-edge animations
    if (m_Animation.IsPlaying())
        return false;

    auto& io = ImGui::GetIO();

    const auto screenMousePos   = io.MousePos;
    const auto screenRect       = ImRect(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + canvasSize);

    // Mouse is over screen, do nothing
    if (screenRect.Contains(screenMousePos))
        return false;

    // Several backend move mouse position to -FLT_MAX to indicate
    // uninitialized/unknown state. To prevent all sorts
    // of math problems, we just ignore such state.
    if (screenMousePos.x <= -FLT_MAX || screenMousePos.y <= -FLT_MAX)
        return false;

    const auto minDistance       = ImVec2(-c_MaxMoveOverEdgeDistance, -c_MaxMoveOverEdgeDistance);
    const auto maxDistance       = ImVec2( c_MaxMoveOverEdgeDistance,  c_MaxMoveOverEdgeDistance);

    const auto screenPointOnEdge  = ImRect_ClosestPoint(screenRect, screenMousePos, true);
    const auto offset             = ImMin(ImMax(screenPointOnEdge - screenMousePos, minDistance), maxDistance);
    const auto relativeOffset     = -offset * io.DeltaTime * c_MaxMoveOverEdgeSpeed;

    m_Scroll = m_Scroll + relativeOffset;

    m_MoveScreenOffset  = relativeOffset;
    m_MovingOverEdge    = true;

    return true;
}

void ed::NavigateAction::StopMoveOverEdge()
{
    if (m_MovingOverEdge)
    {
        Editor->MakeDirty(SaveReasonFlags::Navigation);

        m_MoveScreenOffset  = ImVec2(0, 0);
        m_MovingOverEdge    = false;
    }
}

void ed::NavigateAction::SetWindow(ImVec2 position, ImVec2 size)
{
    m_WindowScreenPos  = position;
    m_WindowScreenSize = size;
}

ImGuiEx::CanvasView ed::NavigateAction::GetView() const
{
    return ImGuiEx::CanvasView(-m_Scroll, m_Zoom);
}

ImVec2 ed::NavigateAction::GetViewOrigin() const
{
    return -m_Scroll;
}

float ed::NavigateAction::GetViewScale() const
{
    return m_Zoom;
}

void ed::NavigateAction::SetViewRect(const ImRect& rect)
{
    auto view = m_Canvas.CalcCenterView(rect);
    m_Scroll = -view.Origin;
    m_Zoom   =  view.Scale;
}

ImRect ed::NavigateAction::GetViewRect() const
{
    return m_Canvas.CalcViewRect(GetView());
}

float ed::NavigateAction::GetNextZoom(float steps)
{
    if (this->Editor->GetConfig().EnableSmoothZoom)
    {
        return MatchSmoothZoom(steps);
    }
    else
    {
        auto fixedSteps = (int)steps;
        return MatchZoom(fixedSteps, m_ZoomLevels[fixedSteps < 0 ? 0 : m_ZoomLevelCount - 1]);
    }
}

float ed::NavigateAction::MatchSmoothZoom(float steps)
{
    const auto power = Editor->GetConfig().SmoothZoomPower;

    const auto newZoom = m_Zoom * powf(power, steps);
    if (newZoom < m_ZoomLevels[0])
        return m_ZoomLevels[0];
    else if (newZoom > m_ZoomLevels[m_ZoomLevelCount - 1])
        return m_ZoomLevels[m_ZoomLevelCount - 1];
    else
        return newZoom;
}

float ed::NavigateAction::MatchZoom(int steps, float fallbackZoom)
{
    auto currentZoomIndex = MatchZoomIndex(steps);
    if (currentZoomIndex < 0)
        return fallbackZoom;

    auto currentZoom = m_ZoomLevels[currentZoomIndex];
    if (fabsf(currentZoom - m_Zoom) > 0.001f)
        return currentZoom;

    auto newIndex = currentZoomIndex + steps;
    if (newIndex >= 0 && newIndex < m_ZoomLevelCount)
        return m_ZoomLevels[newIndex];
    else
        return fallbackZoom;
}

int ed::NavigateAction::MatchZoomIndex(int direction)
{
    int   bestIndex    = -1;
    float bestDistance = 0.0f;

    for (int i = 0; i < m_ZoomLevelCount; ++i)
    {
        auto distance = fabsf(m_ZoomLevels[i] - m_Zoom);
        if (distance < bestDistance || bestIndex < 0)
        {
            bestDistance = distance;
            bestIndex    = i;
        }
    }

    if (bestDistance > 0.001f)
    {
        if (direction > 0)
        {
            ++bestIndex;

            if (bestIndex >= m_ZoomLevelCount)
                bestIndex = m_ZoomLevelCount - 1;
        }
        else if (direction < 0)
        {
            --bestIndex;

            if (bestIndex < 0)
                bestIndex = 0;
        }
    }

    return bestIndex;
}




//------------------------------------------------------------------------------
//
// Size Action
//
//------------------------------------------------------------------------------
ed::SizeAction::SizeAction(EditorContext* editor):
    EditorAction(editor),
    m_IsActive(false),
    m_Clean(false),
    m_SizedNode(nullptr),
    m_Pivot(NodeRegion::None),
    m_Cursor(ImGuiMouseCursor_Arrow)
{
}

ed::EditorAction::AcceptResult ed::SizeAction::Accept(const Control& control)
{
    IM_ASSERT(!m_IsActive);

    if (m_IsActive)
        return False;

    if (control.ActiveNode && IsGroup(control.ActiveNode) && ImGui::IsMouseDragging(Editor->GetConfig().DragButtonIndex, 1))
    {
        //const auto mousePos     = to_point(ImGui::GetMousePos());
        //const auto closestPoint = control.ActiveNode->Bounds.get_closest_point_hollow(mousePos, static_cast<int>(control.ActiveNode->Rounding));

        auto pivot = GetRegion(control.ActiveNode);
        if (pivot != NodeRegion::Header && pivot != NodeRegion::Center)
        {
            m_StartBounds      = control.ActiveNode->m_Bounds;
            m_StartGroupBounds = control.ActiveNode->m_GroupBounds;
            m_LastSize         = control.ActiveNode->m_Bounds.GetSize();
            m_MinimumSize      = ImVec2(0, 0);
            m_LastDragOffset   = ImVec2(0, 0);
            m_Pivot            = pivot;
            m_Cursor           = ChooseCursor(m_Pivot);
            m_SizedNode        = control.ActiveNode;
            m_IsActive         = true;
        }
    }
    else if (control.HotNode && IsGroup(control.HotNode))
    {
        m_Cursor = ChooseCursor(GetRegion(control.HotNode));
        return Possible;
    }

    return m_IsActive ? True : False;
}

bool ed::SizeAction::Process(const Control& control)
{
    if (m_Clean)
    {
        m_Clean = false;

        if (m_SizedNode->m_Bounds.Min != m_StartBounds.Min || m_SizedNode->m_GroupBounds.Min != m_StartGroupBounds.Min)
            Editor->MakeDirty(SaveReasonFlags::Position | SaveReasonFlags::User, m_SizedNode);

        if (m_SizedNode->m_Bounds.GetSize() != m_StartBounds.GetSize() || m_SizedNode->m_GroupBounds.GetSize() != m_StartGroupBounds.GetSize())
            Editor->MakeDirty(SaveReasonFlags::Size | SaveReasonFlags::User, m_SizedNode);

        m_SizedNode = nullptr;
    }

    if (!m_IsActive)
        return false;

    if (control.ActiveNode == m_SizedNode)
    {
        const auto dragOffset = (control.ActiveNode == m_SizedNode) ? ImGui::GetMouseDragDelta(0, 0.0f) : m_LastDragOffset;
        m_LastDragOffset = dragOffset;

        if (m_MinimumSize.x == 0.0f && m_LastSize.x != m_SizedNode->m_Bounds.GetWidth())
            m_MinimumSize.x = m_SizedNode->m_Bounds.GetWidth();
        if (m_MinimumSize.y == 0.0f && m_LastSize.y != m_SizedNode->m_Bounds.GetHeight())
            m_MinimumSize.y = m_SizedNode->m_Bounds.GetHeight();

        auto minimumSize = ImMax(m_MinimumSize, m_StartBounds.GetSize() - m_StartGroupBounds.GetSize());


        auto newBounds = m_StartBounds;

        if ((m_Pivot & NodeRegion::Top) == NodeRegion::Top)
            newBounds.Min.y = ImMin(newBounds.Max.y - minimumSize.y, Editor->AlignPointToGrid(newBounds.Min.y + dragOffset.y));
        if ((m_Pivot & NodeRegion::Bottom) == NodeRegion::Bottom)
            newBounds.Max.y = ImMax(newBounds.Min.y + minimumSize.y, Editor->AlignPointToGrid(newBounds.Max.y + dragOffset.y));
        if ((m_Pivot & NodeRegion::Left) == NodeRegion::Left)
            newBounds.Min.x = ImMin(newBounds.Max.x - minimumSize.x, Editor->AlignPointToGrid(newBounds.Min.x + dragOffset.x));
        if ((m_Pivot & NodeRegion::Right) == NodeRegion::Right)
            newBounds.Max.x = ImMax(newBounds.Min.x + minimumSize.x, Editor->AlignPointToGrid(newBounds.Max.x + dragOffset.x));

        newBounds.Floor();

        m_LastSize = newBounds.GetSize();

        m_SizedNode->m_Bounds      = newBounds;
        m_SizedNode->m_GroupBounds = newBounds;
        m_SizedNode->m_GroupBounds.Min.x -= m_StartBounds.Min.x - m_StartGroupBounds.Min.x;
        m_SizedNode->m_GroupBounds.Min.y -= m_StartBounds.Min.y - m_StartGroupBounds.Min.y;
        m_SizedNode->m_GroupBounds.Max.x -= m_StartBounds.Max.x - m_StartGroupBounds.Max.x;
        m_SizedNode->m_GroupBounds.Max.y -= m_StartBounds.Max.y - m_StartGroupBounds.Max.y;
    }
    else if (!control.ActiveNode)
    {
        m_Clean = true;
        m_IsActive = false;
        return true;
    }

    return m_IsActive;
}

void ed::SizeAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    auto getObjectName = [](Object* object)
    {
        if (!object) return "";
        else if (object->AsNode())  return "Node";
        else if (object->AsPin())   return "Pin";
        else if (object->AsLink())  return "Link";
        else return "";
    };

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", m_IsActive ? "yes" : "no");
    ImGui::Text("    Node: %s (%p)", getObjectName(m_SizedNode), m_SizedNode ? m_SizedNode->m_ID.AsPointer() : nullptr);
    if (m_SizedNode && m_IsActive)
    {
        ImGui::Text("    Bounds: { x=%g y=%g w=%g h=%g }", m_SizedNode->m_Bounds.Min.x, m_SizedNode->m_Bounds.Min.y, m_SizedNode->m_Bounds.GetWidth(), m_SizedNode->m_Bounds.GetHeight());
        ImGui::Text("    Group Bounds: { x=%g y=%g w=%g h=%g }", m_SizedNode->m_GroupBounds.Min.x, m_SizedNode->m_GroupBounds.Min.y, m_SizedNode->m_GroupBounds.GetWidth(), m_SizedNode->m_GroupBounds.GetHeight());
        ImGui::Text("    Start Bounds: { x=%g y=%g w=%g h=%g }", m_StartBounds.Min.x, m_StartBounds.Min.y, m_StartBounds.GetWidth(), m_StartBounds.GetHeight());
        ImGui::Text("    Start Group Bounds: { x=%g y=%g w=%g h=%g }", m_StartGroupBounds.Min.x, m_StartGroupBounds.Min.y, m_StartGroupBounds.GetWidth(), m_StartGroupBounds.GetHeight());
        ImGui::Text("    Minimum Size: { w=%g h=%g }", m_MinimumSize.x, m_MinimumSize.y);
        ImGui::Text("    Last Size: { w=%g h=%g }", m_LastSize.x, m_LastSize.y);
    }
}

ed::NodeRegion ed::SizeAction::GetRegion(Node* node)
{
    return node->GetRegion(ImGui::GetMousePos());
}

ImGuiMouseCursor ed::SizeAction::ChooseCursor(NodeRegion region)
{
    switch (region)
    {
        default:
        case NodeRegion::Center:
            return ImGuiMouseCursor_Arrow;

        case NodeRegion::Top:
        case NodeRegion::Bottom:
            return ImGuiMouseCursor_ResizeNS;

        case NodeRegion::Left:
        case NodeRegion::Right:
            return ImGuiMouseCursor_ResizeEW;

        case NodeRegion::TopLeft:
        case NodeRegion::BottomRight:
            return ImGuiMouseCursor_ResizeNWSE;

        case NodeRegion::TopRight:
        case NodeRegion::BottomLeft:
            return ImGuiMouseCursor_ResizeNESW;
    }
}




//------------------------------------------------------------------------------
//
// Drag Action
//
//------------------------------------------------------------------------------
ed::DragAction::DragAction(EditorContext* editor):
    EditorAction(editor),
    m_IsActive(false),
    m_Clear(false),
    m_DraggedObject(nullptr)
{
}

ed::EditorAction::AcceptResult ed::DragAction::Accept(const Control& control)
{
    IM_ASSERT(!m_IsActive);

    if (m_IsActive)
        return False;

    if (Editor->CanAcceptUserInput() && control.ActiveObject && ImGui::IsMouseDragging(Editor->GetConfig().DragButtonIndex, 1))
    {
        if (!control.ActiveObject->AcceptDrag())
            return False;

        m_DraggedObject = control.ActiveObject;

        m_Objects.resize(0);
        m_Objects.push_back(m_DraggedObject);

        if (Editor->IsSelected(m_DraggedObject))
        {
            for (auto selectedObject : Editor->GetSelectedObjects())
                if (auto selectedNode = selectedObject->AsNode())
                    if (selectedNode != m_DraggedObject && selectedNode->AcceptDrag())
                        m_Objects.push_back(selectedNode);
        }

        auto& io = ImGui::GetIO();
        if (!io.KeyShift)
        {
            std::vector<Node*> groupedNodes;
            for (auto object : m_Objects)
                if (auto node = object->AsNode())
                    node->GetGroupedNodes(groupedNodes, true);

            auto isAlreadyPicked = [this](Node* node)
            {
                return std::find(m_Objects.begin(), m_Objects.end(), node) != m_Objects.end();
            };

            for (auto candidate : groupedNodes)
                if (!isAlreadyPicked(candidate) && candidate->AcceptDrag())
                    m_Objects.push_back(candidate);
        }

        m_IsActive = true;
    }
    else if (control.HotNode && IsGroup(control.HotNode) && control.HotNode->GetRegion(ImGui::GetMousePos()) == NodeRegion::Header)
    {
        return Possible;
    }

    return m_IsActive ? True : False;
}

bool ed::DragAction::Process(const Control& control)
{
    if (m_Clear)
    {
        m_Clear = false;

        for (auto object : m_Objects)
        {
            if (object->EndDrag())
                Editor->MakeDirty(SaveReasonFlags::Position | SaveReasonFlags::User, object->AsNode());
        }

        m_Objects.resize(0);

        m_DraggedObject = nullptr;
    }

    if (!m_IsActive)
        return false;

    if (control.ActiveObject == m_DraggedObject)
    {
        auto dragOffset = ImGui::GetMouseDragDelta(Editor->GetConfig().DragButtonIndex, 0.0f);

        auto draggedOrigin  = m_DraggedObject->DragStartLocation();
        auto alignPivot     = ImVec2(0, 0);

        // TODO: Move this experimental alignment to closes pivot out of internals to node API
        if (auto draggedNode = m_DraggedObject->AsNode())
        {
            float x = FLT_MAX;
            float y = FLT_MAX;

            auto testPivot = [this, &x, &y, &draggedOrigin, &dragOffset, &alignPivot](const ImVec2& pivot)
            {
                auto initial   = draggedOrigin + dragOffset + pivot;
                auto candidate = Editor->AlignPointToGrid(initial) - draggedOrigin - pivot;

                if (ImFabs(candidate.x) < ImFabs(ImMin(x, FLT_MAX)))
                {
                    x = candidate.x;
                    alignPivot.x = pivot.x;
                }

                if (ImFabs(candidate.y) < ImFabs(ImMin(y, FLT_MAX)))
                {
                    y = candidate.y;
                    alignPivot.y = pivot.y;
                }
            };

            for (auto pin = draggedNode->m_LastPin; pin; pin = pin->m_PreviousPin)
            {
                auto pivot = pin->m_Pivot.GetCenter() - draggedNode->m_Bounds.Min;
                testPivot(pivot);
            }

            //testPivot(point(0, 0));
        }

        auto alignedOffset  = Editor->AlignPointToGrid(draggedOrigin + dragOffset + alignPivot) - draggedOrigin - alignPivot;

        if (!ImGui::GetIO().KeyAlt)
            dragOffset = alignedOffset;

        for (auto object : m_Objects)
            object->UpdateDrag(dragOffset);
    }
    else if (!control.ActiveObject)
    {
        m_Clear = true;

        m_IsActive = false;
        return true;
    }

    return m_IsActive;
}

void ed::DragAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    auto getObjectName = [](Object* object)
    {
        if (!object) return "";
        else if (object->AsNode())  return "Node";
        else if (object->AsPin())   return "Pin";
        else if (object->AsLink())  return "Link";
        else return "";
    };

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", m_IsActive ? "yes" : "no");
    ImGui::Text("    Node: %s (%p)", getObjectName(m_DraggedObject), m_DraggedObject ? m_DraggedObject->ID().AsPointer() : nullptr);
}




//------------------------------------------------------------------------------
//
// Select Action
//
//------------------------------------------------------------------------------
ed::SelectAction::SelectAction(EditorContext* editor):
    EditorAction(editor),
    m_IsActive(false),
    m_SelectGroups(false),
    m_SelectLinkMode(false),
    m_CommitSelection(false),
    m_StartPoint(),
    m_Animation(editor)
{
}

ed::EditorAction::AcceptResult ed::SelectAction::Accept(const Control& control)
{
    IM_ASSERT(!m_IsActive);

    if (m_IsActive)
        return False;
    
    // Don't activate selection if we're editing a link (dragging control point)
    if (Editor->m_EditingLink && Editor->m_DraggingControlPointIndex >= 0)
        return False;

    auto& io = ImGui::GetIO();
    m_SelectGroups   = io.KeyShift;
    m_SelectLinkMode = io.KeyAlt;

    m_SelectedObjectsAtStart.clear();

    if (Editor->CanAcceptUserInput() && control.BackgroundHot && ImGui::IsMouseDragging(Editor->GetConfig().SelectButtonIndex, 1))
    {
        m_IsActive = true;
        m_StartPoint = ImGui_GetMouseClickPos(Editor->GetConfig().SelectButtonIndex);
        m_EndPoint   = m_StartPoint;

        // Links and nodes cannot be selected together
        if ((m_SelectLinkMode && Editor->IsAnyNodeSelected()) ||
           (!m_SelectLinkMode && Editor->IsAnyLinkSelected()))
        {
            Editor->ClearSelection();
        }

        if (io.KeyCtrl)
            m_SelectedObjectsAtStart = Editor->GetSelectedObjects();
    }
    else if (control.BackgroundClickButtonIndex == Editor->GetConfig().SelectButtonIndex)
    {
        Editor->ClearSelection();
    }
    else
    {
        Object* clickedObject = control.ClickedNode ? static_cast<Object*>(control.ClickedNode) : static_cast<Object*>(control.ClickedLink);

        if (clickedObject)
        {
            // Links and nodes cannot be selected together
            if ((clickedObject->AsLink() && Editor->IsAnyNodeSelected()) ||
                (clickedObject->AsNode() && Editor->IsAnyLinkSelected()))
            {
                Editor->ClearSelection();
            }

            if (io.KeyCtrl)
                Editor->ToggleObjectSelection(clickedObject);
            else
                Editor->SetSelectedObject(clickedObject);
        }
    }

    if (m_IsActive)
        m_Animation.Stop();

    return m_IsActive ? True : False;
}

bool ed::SelectAction::Process(const Control& control)
{
    IM_UNUSED(control);

    if (m_CommitSelection)
    {
        Editor->ClearSelection();
        for (auto object : m_CandidateObjects)
            Editor->SelectObject(object);

        m_CandidateObjects.clear();

        m_CommitSelection = false;
    }

    if (!m_IsActive)
        return false;

    if (ImGui::IsMouseDragging(Editor->GetConfig().SelectButtonIndex, 0))
    {
        m_EndPoint = ImGui::GetMousePos();

        auto topLeft     = ImVec2(std::min(m_StartPoint.x, m_EndPoint.x), std::min(m_StartPoint.y, m_EndPoint.y));
        auto bottomRight = ImVec2(ImMax(m_StartPoint.x, m_EndPoint.x), ImMax(m_StartPoint.y, m_EndPoint.y));
        auto rect        = ImRect(topLeft, bottomRight);
        if (rect.GetWidth() <= 0)
            rect.Max.x = rect.Min.x + 1;
        if (rect.GetHeight() <= 0)
            rect.Max.y = rect.Min.y + 1;

        vector<Node*> nodes;
        vector<Link*> links;

        if (m_SelectLinkMode)
        {
            Editor->FindLinksInRect(rect, links);
            m_CandidateObjects.assign(links.begin(), links.end());
        }
        else
        {
            Editor->FindNodesInRect(rect, nodes);
            m_CandidateObjects.assign(nodes.begin(), nodes.end());

            if (m_SelectGroups)
            {
                auto endIt = std::remove_if(m_CandidateObjects.begin(), m_CandidateObjects.end(), [](Object* object) { return !IsGroup(object->AsNode()); });
                m_CandidateObjects.erase(endIt, m_CandidateObjects.end());
            }
            else
            {
                auto endIt = std::remove_if(m_CandidateObjects.begin(), m_CandidateObjects.end(), [](Object* object) { return IsGroup(object->AsNode()); });
                m_CandidateObjects.erase(endIt, m_CandidateObjects.end());
            }
        }

        m_CandidateObjects.insert(m_CandidateObjects.end(), m_SelectedObjectsAtStart.begin(), m_SelectedObjectsAtStart.end());
        std::sort(m_CandidateObjects.begin(), m_CandidateObjects.end());
        m_CandidateObjects.erase(std::unique(m_CandidateObjects.begin(), m_CandidateObjects.end()), m_CandidateObjects.end());
    }
    else
    {
        m_IsActive = false;

        m_Animation.Play(c_SelectionFadeOutDuration);

        m_CommitSelection = true;

        return true;
    }

    return m_IsActive;
}

void ed::SelectAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", m_IsActive ? "yes" : "no");
}

void ed::SelectAction::Draw(ImDrawList* drawList)
{
    if (!m_IsActive && !m_Animation.IsPlaying())
        return;

    const auto alpha = m_Animation.IsPlaying() ? ImEasing::EaseOutQuad(1.0f, -1.0f, m_Animation.GetProgress()) : 1.0f;

    const auto fillColor    = Editor->GetColor(m_SelectLinkMode ? StyleColor_LinkSelRect       : StyleColor_NodeSelRect, alpha);
    const auto outlineColor = Editor->GetColor(m_SelectLinkMode ? StyleColor_LinkSelRectBorder : StyleColor_NodeSelRectBorder, alpha);

    drawList->ChannelsSetCurrent(c_BackgroundChannel_SelectionRect);

    auto min  = ImVec2(std::min(m_StartPoint.x, m_EndPoint.x), std::min(m_StartPoint.y, m_EndPoint.y));
    auto max  = ImVec2(ImMax(m_StartPoint.x, m_EndPoint.x), ImMax(m_StartPoint.y, m_EndPoint.y));

    drawList->AddRectFilled(min, max, fillColor);
    drawList->AddRect(min, max, outlineColor);
}




//------------------------------------------------------------------------------
//
// Context Menu Action
//
//------------------------------------------------------------------------------
ed::ContextMenuAction::ContextMenuAction(EditorContext* editor):
    EditorAction(editor),
    m_CandidateMenu(Menu::None),
    m_CurrentMenu(Menu::None),
    m_ContextId()
{
}

ed::EditorAction::AcceptResult ed::ContextMenuAction::Accept(const Control& control)
{
    const auto isPressed  = ImGui::IsMouseClicked(Editor->GetConfig().ContextMenuButtonIndex);
    const auto isReleased = ImGui::IsMouseReleased(Editor->GetConfig().ContextMenuButtonIndex);
    const auto isDragging = ImGui::IsMouseDragging(Editor->GetConfig().ContextMenuButtonIndex, 1);

    if (isPressed || isReleased || isDragging)
    {
        Menu candidateMenu = ContextMenuAction::None;
        ObjectId contextId;

        if (auto hotObejct = control.HotObject)
        {
            if (hotObejct->AsNode())
                candidateMenu = Node;
            else if (hotObejct->AsPin())
            {
                // When hovering over a pin, show node context menu instead
                candidateMenu = Node;
                contextId = hotObejct->AsPin()->m_Node->m_ID;
            }
            else if (hotObejct->AsLink())
                candidateMenu = Link;

            if (candidateMenu != None && contextId == ObjectId())
                contextId = hotObejct->ID();
        }
        else if (control.BackgroundHot)
            candidateMenu = Background;

        if (isPressed)
        {
            m_CandidateMenu = candidateMenu;
            m_ContextId     = contextId;
            return Possible;
        }
        else if (isReleased && m_CandidateMenu == candidateMenu && m_ContextId == contextId)
        {
            m_CurrentMenu   = m_CandidateMenu;
            m_CandidateMenu = ContextMenuAction::None;
            return True;
        }
        else
        {
            m_CandidateMenu = None;
            m_CurrentMenu   = None;
            m_ContextId     = ObjectId();
            return False;
        }
    }

    return False;
}

bool ed::ContextMenuAction::Process(const Control& control)
{
    IM_UNUSED(control);

    m_CandidateMenu = None;
    m_CurrentMenu   = None;
    m_ContextId     = ObjectId();
    return false;
}

void ed::ContextMenuAction::Reject()
{
    m_CandidateMenu = None;
    m_CurrentMenu   = None;
    m_ContextId     = ObjectId();
}

void ed::ContextMenuAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    auto getMenuName = [](Menu menu)
    {
        switch (menu)
        {
            default:
            case None:        return "None";
            case Node:        return "Node";
            case Pin:         return "Pin";
            case Link:        return "Link";
            case Background:  return "Background";
        }
    };


    ImGui::Text("%s:", GetName());
    ImGui::Text("    Menu: %s", getMenuName(m_CurrentMenu));
}

bool ed::ContextMenuAction::ShowNodeContextMenu(NodeId* nodeId)
{
    if (m_CurrentMenu != Node)
        return false;

    *nodeId = m_ContextId.AsNodeId();
    Editor->SetUserContext();
    return true;
}

bool ed::ContextMenuAction::ShowPinContextMenu(PinId* pinId)
{
    if (m_CurrentMenu != Pin)
        return false;

    *pinId = m_ContextId.AsPinId();
    Editor->SetUserContext();
    return true;
}

bool ed::ContextMenuAction::ShowLinkContextMenu(LinkId* linkId)
{
    if (m_CurrentMenu != Link)
        return false;

    *linkId = m_ContextId.AsLinkId();
    Editor->SetUserContext();
    return true;
}

bool ed::ContextMenuAction::ShowBackgroundContextMenu()
{
    if (m_CurrentMenu != Background)
        return false;

    Editor->SetUserContext();
    return true;
}




//------------------------------------------------------------------------------
//
// Cut/Copy/Paste Action
//
//------------------------------------------------------------------------------
ed::ShortcutAction::ShortcutAction(EditorContext* editor):
    EditorAction(editor),
    m_IsActive(false),
    m_InAction(false),
    m_CurrentAction(Action::None),
    m_Context()
{
}

ed::EditorAction::AcceptResult ed::ShortcutAction::Accept(const Control& control)
{
    if (!Editor->IsFocused() || !Editor->AreShortcutsEnabled())
        return False;

    Action candidateAction = None;

    auto& io = ImGui::GetIO();
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_X)))
        candidateAction = Cut;
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C)))
        candidateAction = Copy;
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_V)))
        candidateAction = Paste;
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(GetKeyIndexForD()))
        candidateAction = Duplicate;
    if (!io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Space)))
        candidateAction = CreateNode;

    if (candidateAction != None)
    {
        if (candidateAction != Paste && candidateAction != CreateNode)
        {
            auto& selection = Editor->GetSelectedObjects();
            if (!selection.empty())
            {
                // #TODO: Find a way to simplify logic.

                m_Context.assign(selection.begin(), selection.end());

                // Expand groups
                vector<Node*> extra;
                for (auto object : m_Context)
                {
                    auto node = object->AsNode();
                    if (IsGroup(node))
                        node->GetGroupedNodes(extra, true);
                }

                // Apply groups and remove duplicates
                if (!extra.empty())
                {
                    m_Context.insert(m_Context.end(), extra.begin(), extra.end());
                    std::sort(m_Context.begin(), m_Context.end());
                    m_Context.erase(std::unique(m_Context.begin(), m_Context.end()), m_Context.end());
                }
            }
            else if (control.HotObject && control.HotObject->IsSelectable() && !IsGroup(control.HotObject->AsNode()))
            {
                m_Context.push_back(control.HotObject);
            }

            if (m_Context.empty())
                return False;

            // Does copying only links make sense?
            //const auto hasOnlyLinks = std::all_of(Context.begin(), Context.end(), [](Object* object) { return object->AsLink() != nullptr; });
            //if (hasOnlyLinks)
            //    return False;

            // If no links are selected, pick all links between nodes within context
            const auto hasAnyLinks = std::any_of(m_Context.begin(), m_Context.end(), [](Object* object) { return object->AsLink() != nullptr; });
            if (!hasAnyLinks && m_Context.size() > 1) // one node cannot make connection to anything
            {
                // Collect nodes in sorted vector viable for binary search
                std::vector<ObjectWrapper<Node>> nodes;

                nodes.reserve(m_Context.size());
                std::for_each(m_Context.begin(), m_Context.end(), [&nodes](Object* object) { if (auto node = object->AsNode()) nodes.push_back({node->m_ID, node}); });

                std::sort(nodes.begin(), nodes.end());

                auto isNodeInContext = [&nodes](NodeId nodeId)
                {
                    return std::binary_search(nodes.begin(), nodes.end(), ObjectWrapper<Node>{nodeId, nullptr});
                };

                // Collect links connected to nodes and drop those reaching out of context
                std::vector<Link*> links;

                for (auto node : nodes)
                    Editor->FindLinksForNode(node.m_ID, links, true);

                // Remove duplicates
                std::sort(links.begin(), links.end());
                links.erase(std::unique(links.begin(), links.end()), links.end());

                // Drop out of context links
                links.erase(std::remove_if(links.begin(), links.end(), [&isNodeInContext](Link* link)
                {
                    return !isNodeInContext(link->m_StartPin->m_Node->m_ID) || !isNodeInContext(link->m_EndPin->m_Node->m_ID);
                }), links.end());

                // Append links and remove duplicates
                m_Context.insert(m_Context.end(), links.begin(), links.end());
            }
        }
        else
            m_Context.resize(0);

        m_IsActive      = true;
        m_CurrentAction = candidateAction;

        return True;
    }

    return False;
}

bool ed::ShortcutAction::Process(const Control& control)
{
    IM_UNUSED(control);

    m_IsActive        = false;
    m_CurrentAction   = None;
    m_Context.resize(0);
    return false;
}

void ed::ShortcutAction::Reject()
{
    m_IsActive        = false;
    m_CurrentAction   = None;
    m_Context.resize(0);
}

void ed::ShortcutAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    auto getActionName = [](Action action)
    {
        switch (action)
        {
            default:
            case None:       return "None";
            case Cut:        return "Cut";
            case Copy:       return "Copy";
            case Paste:      return "Paste";
            case Duplicate:  return "Duplicate";
            case CreateNode: return "CreateNode";
        }
    };

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Action: %s", getActionName(m_CurrentAction));
}

bool ed::ShortcutAction::Begin()
{
    if (m_IsActive)
        m_InAction = true;
    return m_IsActive;
}

void ed::ShortcutAction::End()
{
    if (m_IsActive)
        m_InAction = false;
}

bool ed::ShortcutAction::AcceptCut()
{
    IM_ASSERT(m_InAction);
    return m_CurrentAction == Cut;
}

bool ed::ShortcutAction::AcceptCopy()
{
    IM_ASSERT(m_InAction);
    return m_CurrentAction == Copy;
}

bool ed::ShortcutAction::AcceptPaste()
{
    IM_ASSERT(m_InAction);
    return m_CurrentAction == Paste;
}

bool ed::ShortcutAction::AcceptDuplicate()
{
    IM_ASSERT(m_InAction);
    return m_CurrentAction == Duplicate;
}

bool ed::ShortcutAction::AcceptCreateNode()
{
    IM_ASSERT(m_InAction);
    return m_CurrentAction == CreateNode;
}




//------------------------------------------------------------------------------
//
// Create Item Action
//
//------------------------------------------------------------------------------
ed::CreateItemAction::CreateItemAction(EditorContext* editor):
    EditorAction(editor),
    m_InActive(false),
    m_NextStage(None),
    m_CurrentStage(None),
    m_ItemType(NoItem),
    m_UserAction(Unknown),
    m_LinkColor(IM_COL32_WHITE),
    m_LinkThickness(1.0f),
    m_LinkStart(nullptr),
    m_LinkEnd(nullptr),

    m_IsActive(false),
    m_DraggedPin(nullptr),

    m_IsInGlobalSpace(false)
{
}

ed::EditorAction::AcceptResult ed::CreateItemAction::Accept(const Control& control)
{
    IM_ASSERT(!m_IsActive);

    if (m_IsActive)
        return EditorAction::False;

    // Don't start link creation if a node was clicked (user wants to drag the node)
    // Check if a node was clicked and can be dragged - this takes priority over pin dragging
    if (control.ClickedNode && control.ClickedNode->AcceptDrag())
    {
        // Node was clicked and can be dragged - let DragAction handle it
        return EditorAction::False;
    }

    // Check if we're dragging from an active pin (link creation can only start from pin pivots)
    if (control.ActivePin && ImGui::IsMouseDragging(Editor->GetConfig().DragButtonIndex, 1))
    {
        // CRITICAL: If ActiveObject is a node (not a pin), prefer node dragging over link creation
        // This handles cases where a pin overlaps a node but the user wants to drag the node
        if (control.ActiveObject && control.ActiveObject->AsNode() && !control.ActiveObject->AsPin())
        {
            // ActiveObject is a node - let DragAction handle node dragging
            return EditorAction::False;
        }

        // If a node was clicked BUT no pin was clicked, prefer node dragging
        // This handles the case where clicking on node body overlaps with a pin
        if (control.ClickedNode && !control.ClickedPin)
        {
            // Node body was clicked (not a pin) - let DragAction handle node dragging
            return EditorAction::False;
        }

        // Either:
        // 1. Pin was clicked (ClickedPin is set), OR
        // 2. Pin is active and ActiveObject is not a node (allows pin dragging even if ClickedPin cleared)
        // Start link creation from the active pin
        m_DraggedPin = control.ActivePin;
        DragStart(m_DraggedPin);

        Editor->ClearSelection();
        m_IsActive = true;
        return EditorAction::True;
    }
    else if (control.HotPin)
    {
        // Pin is hovered but not clicked/dragging yet - show possible link creation cursor
        return EditorAction::Possible;
    }
    
    return EditorAction::False;
}

bool ed::CreateItemAction::Process(const Control& control)
{
    IM_ASSERT(m_IsActive);

    if (!m_IsActive)
        return false;

    if (m_DraggedPin && control.ActivePin == m_DraggedPin && (m_CurrentStage == Possible))
    {
        const auto draggingFromSource = (m_DraggedPin->m_Kind == PinKind::Output);

        ed::Pin cursorPin(Editor, 0, draggingFromSource ? PinKind::Input : PinKind::Output);
        cursorPin.m_Pivot    = ImRect(ImGui::GetMousePos(), ImGui::GetMousePos());
        cursorPin.m_Dir      = -m_DraggedPin->m_Dir;
        cursorPin.m_Strength =  m_DraggedPin->m_Strength;

        ed::Link candidate(Editor, 0);
        candidate.m_Color    = m_LinkColor;
        candidate.m_StartPin = draggingFromSource ? m_DraggedPin : &cursorPin;
        candidate.m_EndPin   = draggingFromSource ? &cursorPin : m_DraggedPin;

        ed::Pin*& freePin  = draggingFromSource ? candidate.m_EndPin : candidate.m_StartPin;

        if (control.HotPin)
        {
            DropPin(control.HotPin);

            if (m_UserAction == UserAccept)
                freePin = control.HotPin;
        }
        else if (control.BackgroundHot)
            DropNode();
        else
            DropNothing();

        auto drawList = Editor->GetDrawList();
        drawList->ChannelsSetCurrent(c_LinkChannel_NewLink);

        candidate.UpdateEndpoints();
        candidate.Draw(drawList, m_LinkColor, m_LinkThickness);
    }
    else if (m_CurrentStage == Possible || !control.ActivePin)
    {
        if (!Editor->CanAcceptUserInput())
        {
            m_DraggedPin = nullptr;
            DropNothing();
        }

        DragEnd();
        m_IsActive = false;
    }

    return m_IsActive;
}

void ed::CreateItemAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    auto getStageName = [](Stage stage)
    {
        switch (stage)
        {
            case None:     return "None";
            case Possible: return "Possible";
            case Create:   return "Create";
            default:       return "<unknown>";
        }
    };

    auto getActionName = [](Action action)
    {
        switch (action)
        {
            default:
            case Unknown:     return "Unknown";
            case UserReject:  return "Reject";
            case UserAccept:  return "Accept";
        }
    };

    auto getItemName = [](Type item)
    {
        switch (item)
        {
            default:
            case NoItem: return "None";
            case Node:   return "Node";
            case Link:   return "Link";
        }
    };

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Stage: %s", getStageName(m_CurrentStage));
    ImGui::Text("    User Action: %s", getActionName(m_UserAction));
    ImGui::Text("    Item Type: %s", getItemName(m_ItemType));
}

void ed::CreateItemAction::SetStyle(ImU32 color, float thickness)
{
    m_LinkColor     = color;
    m_LinkThickness = thickness;
}

bool ed::CreateItemAction::Begin()
{
    IM_ASSERT(false == m_InActive);

    m_InActive        = true;
    m_CurrentStage    = m_NextStage;
    m_UserAction      = Unknown;
    m_LinkColor       = IM_COL32_WHITE;
    m_LinkThickness   = 1.0f;

    if (m_CurrentStage == None)
        return false;

    m_LastChannel = Editor->GetDrawList()->_Splitter._Current;

    return true;
}

void ed::CreateItemAction::End()
{
    // Handle case where End() is called without Begin() being called first
    // This can happen if EndCreate() is called unconditionally in user code
    if (!m_InActive)
        return;

    if (m_IsInGlobalSpace)
    {
        ImGui::PopClipRect();
        Editor->Resume(SuspendFlags::KeepSplitter);

        // Only restore channel if draw list is valid
        if (Editor->GetDrawList())
        {
            auto currentChannel = Editor->GetDrawList()->_Splitter._Current;
            if (currentChannel != m_LastChannel && m_LastChannel >= 0)
                Editor->GetDrawList()->ChannelsSetCurrent(m_LastChannel);
        }

        m_IsInGlobalSpace = false;
    }

    m_InActive = false;
}

void ed::CreateItemAction::DragStart(Pin* startPin)
{
    IM_ASSERT(!m_InActive);

    m_NextStage = Possible;
    m_LinkStart = startPin;
    m_LinkEnd   = nullptr;
}

void ed::CreateItemAction::DragEnd()
{
    IM_ASSERT(!m_InActive);

    if (m_CurrentStage == Possible && m_UserAction == UserAccept)
    {
        m_NextStage = Create;
    }
    else
    {
        m_NextStage = None;
        m_ItemType  = NoItem;
        m_LinkStart = nullptr;
        m_LinkEnd   = nullptr;
    }
}

void ed::CreateItemAction::DropPin(Pin* endPin)
{
    IM_ASSERT(!m_InActive);

    m_ItemType = Link;
    m_LinkEnd  = endPin;
}

void ed::CreateItemAction::DropNode()
{
    IM_ASSERT(!m_InActive);

    m_ItemType = Node;
    m_LinkEnd  = nullptr;
}

void ed::CreateItemAction::DropNothing()
{
    IM_ASSERT(!m_InActive);

    m_ItemType = NoItem;
    m_LinkEnd  = nullptr;
}

ed::CreateItemAction::Result ed::CreateItemAction::RejectItem()
{
    IM_ASSERT(m_InActive);

    if (!m_InActive || m_CurrentStage == None || m_ItemType == NoItem)
        return Indeterminate;

    m_UserAction = UserReject;

    return True;
}

ed::CreateItemAction::Result ed::CreateItemAction::AcceptItem()
{
    IM_ASSERT(m_InActive);

    if (!m_InActive || m_CurrentStage == None || m_ItemType == NoItem)
        return Indeterminate;

    m_UserAction = UserAccept;

    if (m_CurrentStage == Create)
    {
        m_NextStage = None;
        m_ItemType  = NoItem;
        m_LinkStart = nullptr;
        m_LinkEnd   = nullptr;
        return True;
    }
    else
        return False;
}

ed::CreateItemAction::Result ed::CreateItemAction::QueryLink(PinId* startId, PinId* endId)
{
    // Note: m_InActive check handles case where Begin() wasn't called
    if (!m_InActive || m_CurrentStage == None || m_ItemType != Link)
        return Indeterminate;

    auto linkStartId = m_LinkStart->m_ID;
    auto linkEndId   = m_LinkEnd->m_ID;

    *startId = linkStartId;
    *endId   = linkEndId;

    Editor->SetUserContext(true);

    if (!m_IsInGlobalSpace)
    {
        Editor->Suspend(SuspendFlags::KeepSplitter);

        auto rect = Editor->GetRect();
        ImGui::PushClipRect(rect.Min + ImVec2(1, 1), rect.Max - ImVec2(1, 1), false);
        m_IsInGlobalSpace = true;
    }

    return True;
}

ed::CreateItemAction::Result ed::CreateItemAction::QueryNode(PinId* pinId)
{
    // Note: m_InActive check handles case where Begin() wasn't called
    if (!m_InActive || m_CurrentStage == None || m_ItemType != Node)
        return Indeterminate;

    *pinId = m_LinkStart ? m_LinkStart->m_ID : 0;

    Editor->SetUserContext(true);

    if (!m_IsInGlobalSpace)
    {
        Editor->Suspend(SuspendFlags::KeepSplitter);

        auto rect = Editor->GetRect();
        ImGui::PushClipRect(rect.Min + ImVec2(1, 1), rect.Max - ImVec2(1, 1), false);
        m_IsInGlobalSpace = true;
    }

    return True;
}




//------------------------------------------------------------------------------
//
// Delete Items Action
//
//------------------------------------------------------------------------------
ed::DeleteItemsAction::DeleteItemsAction(EditorContext* editor):
    EditorAction(editor),
    m_IsActive(false),
    m_InInteraction(false),
    m_CurrentItemType(Unknown),
    m_UserAction(Undetermined)
{
}

void ed::DeleteItemsAction::DeleteDeadLinks(NodeId nodeId)
{
    vector<ed::Link*> links;
    Editor->FindLinksForNode(nodeId, links, true);
    for (auto link : links)
    {
        link->m_DeleteOnNewFrame = true;

        auto it = std::find(m_CandidateObjects.begin(), m_CandidateObjects.end(), link);
        if (it != m_CandidateObjects.end())
            continue;

        m_CandidateObjects.push_back(link);
    }
}

void ed::DeleteItemsAction::DeleteDeadPins(NodeId nodeId)
{
    auto node = Editor->FindNode(nodeId);
    if (!node)
        return;

    for (auto pin = node->m_LastPin; pin; pin = pin->m_PreviousPin)
        pin->m_DeleteOnNewFrame = true;
}

ed::EditorAction::AcceptResult ed::DeleteItemsAction::Accept(const Control& control)
{
    IM_ASSERT(!m_IsActive);

    if (m_IsActive)
        return False;

    auto& io = ImGui::GetIO();
    if (Editor->CanAcceptUserInput() && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)) && Editor->AreShortcutsEnabled())
    {
        auto& selection = Editor->GetSelectedObjects();
        if (!selection.empty())
        {
            m_CandidateObjects = selection;
            m_IsActive = true;
            return True;
        }
    }
    else if (control.ClickedLink && io.KeyAlt)
    {
        m_CandidateObjects.clear();
        m_CandidateObjects.push_back(control.ClickedLink);
        m_IsActive = true;
        return True;
    }

    else if (!m_ManuallyDeletedObjects.empty())
    {
        m_CandidateObjects = m_ManuallyDeletedObjects;
        m_ManuallyDeletedObjects.clear();
        m_IsActive = true;
        return True;
    }

    return m_IsActive ? True : False;
}

bool ed::DeleteItemsAction::Process(const Control& control)
{
    IM_UNUSED(control);

    if (!m_IsActive)
        return false;

    m_IsActive = false;
    return true;
}

void ed::DeleteItemsAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    //auto getObjectName = [](Object* object)
    //{
    //    if (!object) return "";
    //    else if (object->AsNode()) return "Node";
    //    else if (object->AsPin())  return "Pin";
    //    else if (object->AsLink()) return "Link";
    //    else return "";
    //};

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", m_IsActive ? "yes" : "no");
    //ImGui::Text("    Node: %s (%d)", getObjectName(DeleteItemsgedNode), DeleteItemsgedNode ? DeleteItemsgedNode->ID : 0);
}

bool ed::DeleteItemsAction::Add(Object* object)
{
    if (Editor->GetCurrentAction() != nullptr)
        return false;

    m_ManuallyDeletedObjects.push_back(object);

    return true;
}

bool ed::DeleteItemsAction::Begin()
{
    if (!m_IsActive)
        return false;

    IM_ASSERT(!m_InInteraction);
    m_InInteraction = true;

    m_CurrentItemType = Unknown;
    m_UserAction      = Undetermined;

    return m_IsActive;
}

void ed::DeleteItemsAction::End()
{
    if (!m_IsActive)
        return;

    IM_ASSERT(m_InInteraction);
    m_InInteraction = false;
}

bool ed::DeleteItemsAction::QueryLink(LinkId* linkId, PinId* startId, PinId* endId)
{
    ObjectId objectId;
    if (!QueryItem(&objectId, Link))
        return false;

    if (auto id = objectId.AsLinkId())
        *linkId = id;
    else
        return false;

    if (startId || endId)
    {
        auto link = Editor->FindLink(*linkId);
        if (startId)
            *startId = link->m_StartPin->m_ID;
        if (endId)
            *endId = link->m_EndPin->m_ID;
    }

    return true;
}

bool ed::DeleteItemsAction::QueryNode(NodeId* nodeId)
{
    ObjectId objectId;
    if (!QueryItem(&objectId, Node))
        return false;

    if (auto id = objectId.AsNodeId())
        *nodeId = id;
    else
        return false;

    return true;
}

bool ed::DeleteItemsAction::QueryItem(ObjectId* itemId, IteratorType itemType)
{
    if (!m_InInteraction)
        return false;

    if (m_CurrentItemType != itemType)
    {
        m_CurrentItemType    = itemType;
        m_CandidateItemIndex = 0;
    }
    else if (m_UserAction == Undetermined)
    {
        RejectItem();
    }

    m_UserAction = Undetermined;

    auto itemCount = (int)m_CandidateObjects.size();
    while (m_CandidateItemIndex < itemCount)
    {
        auto item = m_CandidateObjects[m_CandidateItemIndex];
        if (itemType == Node)
        {
            if (auto node = item->AsNode())
            {
                *itemId = node->m_ID;
                return true;
            }
        }
        else if (itemType == Link)
        {
            if (auto link = item->AsLink())
            {
                *itemId = link->m_ID;
                return true;
            }
        }

        ++m_CandidateItemIndex;
    }

    if (m_CandidateItemIndex == itemCount)
        m_CurrentItemType = Unknown;

    return false;
}

bool ed::DeleteItemsAction::AcceptItem(bool deleteDependencies)
{
    if (!m_InInteraction)
        return false;

    m_UserAction = Accepted;

    RemoveItem(deleteDependencies);

    return true;
}

void ed::DeleteItemsAction::RejectItem()
{
    if (!m_InInteraction)
        return;

    m_UserAction = Rejected;

    DropCurrentItem();
}

void ed::DeleteItemsAction::RemoveItem(bool deleteDependencies)
{
    auto item = DropCurrentItem();

    Editor->DeselectObject(item);

    Editor->RemoveSettings(item);

    item->m_DeleteOnNewFrame = true;

    if (deleteDependencies && m_CurrentItemType == Node)
    {
        auto node = item->ID().AsNodeId();
        DeleteDeadLinks(node);
        DeleteDeadPins(node);
    }

    if (m_CurrentItemType == Link)
        Editor->NotifyLinkDeleted(item->AsLink());
}

ed::Object* ed::DeleteItemsAction::DropCurrentItem()
{
    auto item = m_CandidateObjects[m_CandidateItemIndex];
    m_CandidateObjects.erase(m_CandidateObjects.begin() + m_CandidateItemIndex);

    return item;
}




//------------------------------------------------------------------------------
//
// Node Builder
//
//------------------------------------------------------------------------------
ed::NodeBuilder::NodeBuilder(EditorContext* editor):
    Editor(editor),
    m_CurrentNode(nullptr),
    m_CurrentPin(nullptr)
{
}

ed::NodeBuilder::~NodeBuilder()
{
    m_Splitter.ClearFreeMemory();
    m_PinSplitter.ClearFreeMemory();
}

void ed::NodeBuilder::Begin(NodeId nodeId)
{
    IM_ASSERT(nullptr == m_CurrentNode);

    m_CurrentNode = Editor->GetNode(nodeId);

    Editor->UpdateNodeState(m_CurrentNode);

    if (m_CurrentNode->m_CenterOnScreen)
    {
        auto bounds = Editor->GetViewRect();
        auto offset = bounds.GetCenter() - m_CurrentNode->m_Bounds.GetCenter();

        if (ImLengthSqr(offset) > 0)
        {
            if (IsGroup(m_CurrentNode))
            {
                std::vector<Node*> groupedNodes;
                m_CurrentNode->GetGroupedNodes(groupedNodes);
                groupedNodes.push_back(m_CurrentNode);

                for (auto node : groupedNodes)
                {
                    node->m_Bounds.Translate(ImFloor(offset));
                    node->m_GroupBounds.Translate(ImFloor(offset));
                    Editor->MakeDirty(SaveReasonFlags::Position | SaveReasonFlags::User, node);
                }
            }
            else
            {
                m_CurrentNode->m_Bounds.Translate(ImFloor(offset));
                m_CurrentNode->m_GroupBounds.Translate(ImFloor(offset));
                Editor->MakeDirty(SaveReasonFlags::Position | SaveReasonFlags::User, m_CurrentNode);
            }
        }

        m_CurrentNode->m_CenterOnScreen = false;
    }

    // Position node on screen
    ImGui::SetCursorScreenPos(m_CurrentNode->m_Bounds.Min);

    auto& editorStyle = Editor->GetStyle();

    const auto alpha = ImGui::GetStyle().Alpha;

    m_CurrentNode->m_IsLive           = true;
    m_CurrentNode->m_LastPin          = nullptr;
    m_CurrentNode->m_Color            = Editor->GetColor(StyleColor_NodeBg, alpha);
    m_CurrentNode->m_BorderColor      = Editor->GetColor(StyleColor_NodeBorder, alpha);
    m_CurrentNode->m_BorderWidth      = editorStyle.NodeBorderWidth;
    m_CurrentNode->m_Rounding         = editorStyle.NodeRounding;
    m_CurrentNode->m_GroupColor       = Editor->GetColor(StyleColor_GroupBg, alpha);
    m_CurrentNode->m_GroupBorderColor = Editor->GetColor(StyleColor_GroupBorder, alpha);
    m_CurrentNode->m_GroupBorderWidth = editorStyle.GroupBorderWidth;
    m_CurrentNode->m_GroupRounding    = editorStyle.GroupRounding;
    m_CurrentNode->m_HighlightConnectedLinks = editorStyle.HighlightConnectedLinks != 0.0f;

    m_IsGroup = false;

    // Grow channel list and select user channel
    if (auto drawList = Editor->GetDrawList())
    {
        m_CurrentNode->m_Channel = drawList->_Splitter._Count;
        ImDrawList_ChannelsGrow(drawList, drawList->_Splitter._Count + c_ChannelsPerNode);
        drawList->ChannelsSetCurrent(m_CurrentNode->m_Channel + c_NodeContentChannel);

        m_Splitter.Clear();
        ImDrawList_SwapSplitter(drawList, m_Splitter);
    }

    // Begin outer group
    ImGui::BeginGroup();

    // Apply frame padding. Begin inner group if necessary.
    if (editorStyle.NodePadding.x != 0 || editorStyle.NodePadding.y != 0 || editorStyle.NodePadding.z != 0 || editorStyle.NodePadding.w != 0)
    {
        ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(editorStyle.NodePadding.x, editorStyle.NodePadding.y));
        ImGui::BeginGroup();
    }
}

void ed::NodeBuilder::End()
{
    IM_ASSERT(nullptr != m_CurrentNode);

    if (auto drawList = Editor->GetDrawList())
    {
        IM_ASSERT(drawList->_Splitter._Count == 1); // Did you forgot to call drawList->ChannelsMerge()?
        ImDrawList_SwapSplitter(drawList, m_Splitter);
    }

    // Apply frame padding. This must be done in this convoluted way if outer group
    // size must contain inner group padding.
    auto& editorStyle = Editor->GetStyle();
    if (editorStyle.NodePadding.x != 0 || editorStyle.NodePadding.y != 0 || editorStyle.NodePadding.z != 0 || editorStyle.NodePadding.w != 0)
    {
        ImGui::EndGroup();
        ImGui::SameLine(0, editorStyle.NodePadding.z);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGui::Dummy(ImVec2(0, 0)); // bump cursor at the end of the line and move to next one
        ImGui::Dummy(ImVec2(0, editorStyle.NodePadding.w)); // apply padding
        ImGui::PopStyleVar();
    }

    // End outer group.
    ImGui::EndGroup();

    m_NodeRect = ImGui_GetItemRect();
    m_NodeRect.Floor();

    if (m_CurrentNode->m_Bounds.GetSize() != m_NodeRect.GetSize())
    {
        m_CurrentNode->m_Bounds.Max = m_CurrentNode->m_Bounds.Min + m_NodeRect.GetSize();
        Editor->MakeDirty(SaveReasonFlags::Size, m_CurrentNode);
    }

    if (m_IsGroup)
    {
        // Groups cannot have pins. Discard them.
        for (auto pin = m_CurrentNode->m_LastPin; pin; pin = pin->m_PreviousPin)
            pin->Reset();

        m_CurrentNode->m_Type        = NodeType::Group;
        m_CurrentNode->m_GroupBounds = m_GroupBounds;
        m_CurrentNode->m_LastPin     = nullptr;
    }
    else
        m_CurrentNode->m_Type        = NodeType::Node;

    m_CurrentNode = nullptr;
}

void ed::NodeBuilder::BeginPin(PinId pinId, PinKind kind)
{
    IM_ASSERT(nullptr != m_CurrentNode);
    IM_ASSERT(nullptr == m_CurrentPin);
    IM_ASSERT(false   == m_IsGroup);

    auto& editorStyle = Editor->GetStyle();

    m_CurrentPin = Editor->GetPin(pinId, kind);
    m_CurrentPin->m_Node = m_CurrentNode;

    m_CurrentPin->m_IsLive      = true;
    m_CurrentPin->m_Color       = Editor->GetColor(StyleColor_PinRect);
    m_CurrentPin->m_BorderColor = Editor->GetColor(StyleColor_PinRectBorder);
    m_CurrentPin->m_BorderWidth = editorStyle.PinBorderWidth;
    m_CurrentPin->m_Rounding    = editorStyle.PinRounding;
    m_CurrentPin->m_Corners     = static_cast<int>(editorStyle.PinCorners);
    m_CurrentPin->m_Radius      = editorStyle.PinRadius;
    m_CurrentPin->m_ArrowSize   = editorStyle.PinArrowSize;
    m_CurrentPin->m_ArrowWidth  = editorStyle.PinArrowWidth;
    m_CurrentPin->m_Dir         = kind == PinKind::Output ? editorStyle.SourceDirection : editorStyle.TargetDirection;
    m_CurrentPin->m_Strength    = editorStyle.LinkStrength;
    m_CurrentPin->m_SnapLinkToDir = editorStyle.SnapLinkToPinDir != 0.0f;

    m_CurrentPin->m_PreviousPin = m_CurrentNode->m_LastPin;
    m_CurrentNode->m_LastPin    = m_CurrentPin;

    m_PivotAlignment          = editorStyle.PivotAlignment;
    m_PivotSize               = editorStyle.PivotSize;
    m_PivotScale              = editorStyle.PivotScale;
    m_ResolvePinRect          = true;
    m_ResolvePivot            = true;

    if (auto drawList = Editor->GetDrawList())
    {
        m_PinSplitter.Clear();
        ImDrawList_SwapSplitter(drawList, m_PinSplitter);
    }

    ImGui::BeginGroup();
}

void ed::NodeBuilder::EndPin()
{
    IM_ASSERT(nullptr != m_CurrentPin);

    if (auto drawList = Editor->GetDrawList())
    {
        IM_ASSERT(drawList->_Splitter._Count == 1); // Did you forgot to call drawList->ChannelsMerge()?
        ImDrawList_SwapSplitter(drawList, m_PinSplitter);
    }

    ImGui::EndGroup();

    if (m_ResolvePinRect)
        m_CurrentPin->m_Bounds = ImGui_GetItemRect();

    if (m_ResolvePivot)
    {
        auto& pinRect = m_CurrentPin->m_Bounds;

        if (m_PivotSize.x < 0)
            m_PivotSize.x = pinRect.GetWidth();
        if (m_PivotSize.y < 0)
            m_PivotSize.y = pinRect.GetHeight();

        m_CurrentPin->m_Pivot.Min = pinRect.Min + ImMul(pinRect.GetSize(), m_PivotAlignment);
        m_CurrentPin->m_Pivot.Max = m_CurrentPin->m_Pivot.Min + ImMul(m_PivotSize, m_PivotScale);
    }

    // #debug: Draw pin bounds
    //Editor->GetDrawList()->AddRect(m_CurrentPin->m_Bounds.Min, m_CurrentPin->m_Bounds.Max, IM_COL32(255, 255, 0, 255));

    // #debug: Draw pin pivot rectangle
    //Editor->GetDrawList()->AddRect(m_CurrentPin->m_Pivot.Min, m_CurrentPin->m_Pivot.Max, IM_COL32(255, 0, 255, 255));

    m_CurrentPin = nullptr;
}

void ed::NodeBuilder::PinRect(const ImVec2& a, const ImVec2& b)
{
    IM_ASSERT(nullptr != m_CurrentPin);

    m_CurrentPin->m_Bounds = ImRect(a, b);
    m_CurrentPin->m_Bounds.Floor();
    m_ResolvePinRect     = false;
}

void ed::NodeBuilder::PinPivotRect(const ImVec2& a, const ImVec2& b)
{
    IM_ASSERT(nullptr != m_CurrentPin);

    m_CurrentPin->m_Pivot = ImRect(a, b);
    m_ResolvePivot      = false;
}

void ed::NodeBuilder::PinPivotSize(const ImVec2& size)
{
    IM_ASSERT(nullptr != m_CurrentPin);

    m_PivotSize    = size;
    m_ResolvePivot = true;
}

void ed::NodeBuilder::PinPivotScale(const ImVec2& scale)
{
    IM_ASSERT(nullptr != m_CurrentPin);

    m_PivotScale   = scale;
    m_ResolvePivot = true;
}

void ed::NodeBuilder::PinPivotAlignment(const ImVec2& alignment)
{
    IM_ASSERT(nullptr != m_CurrentPin);

    m_PivotAlignment = alignment;
    m_ResolvePivot   = true;
}

void ed::NodeBuilder::Group(const ImVec2& size)
{
    IM_ASSERT(nullptr != m_CurrentNode);
    IM_ASSERT(nullptr == m_CurrentPin);
    IM_ASSERT(false   == m_IsGroup);

    m_IsGroup = true;

    if (IsGroup(m_CurrentNode))
        ImGui::Dummy(m_CurrentNode->m_GroupBounds.GetSize());
    else
        ImGui::Dummy(size);

    m_GroupBounds = ImGui_GetItemRect();
    m_GroupBounds.Floor();
}

ImDrawList* ed::NodeBuilder::GetUserBackgroundDrawList() const
{
    return GetUserBackgroundDrawList(m_CurrentNode);
}

ImDrawList* ed::NodeBuilder::GetUserBackgroundDrawList(Node* node) const
{
    if (node && node->m_IsLive)
    {
        auto drawList = Editor->GetDrawList();
        drawList->ChannelsSetCurrent(node->m_Channel + c_NodeUserBackgroundChannel);
        return drawList;
    }
    else
        return nullptr;
}




//------------------------------------------------------------------------------
//
// Node Builder
//
//------------------------------------------------------------------------------
ed::HintBuilder::HintBuilder(EditorContext* editor):
    Editor(editor),
    m_IsActive(false),
    m_CurrentNode(nullptr)
{
}

bool ed::HintBuilder::Begin(NodeId nodeId)
{
    IM_ASSERT(nullptr == m_CurrentNode);

    auto& view = Editor->GetView();
    auto& rect = Editor->GetRect();

    const float c_min_zoom = 0.75f;
    const float c_max_zoom = 0.50f;

    if (view.Scale > 0.75f)
        return false;

    auto node = Editor->FindNode(nodeId);
    if (!IsGroup(node))
        return false;

    m_CurrentNode = node;

    m_LastChannel = Editor->GetDrawList()->_Splitter._Current;

    Editor->Suspend(SuspendFlags::KeepSplitter);

    const auto alpha = ImMax(0.0f, std::min(1.0f, (view.Scale - c_min_zoom) / (c_max_zoom - c_min_zoom)));

    Editor->GetDrawList()->ChannelsSetCurrent(c_UserChannel_HintsBackground);
    ImGui::PushClipRect(rect.Min + ImVec2(1, 1), rect.Max - ImVec2(1, 1), false);

    Editor->GetDrawList()->ChannelsSetCurrent(c_UserChannel_Hints);
    ImGui::PushClipRect(rect.Min + ImVec2(1, 1), rect.Max - ImVec2(1, 1), false);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    m_IsActive = true;

    return true;
}

void ed::HintBuilder::End()
{
    if (!m_IsActive)
        return;

    ImGui::PopStyleVar();

    Editor->GetDrawList()->ChannelsSetCurrent(c_UserChannel_Hints);
    ImGui::PopClipRect();

    Editor->GetDrawList()->ChannelsSetCurrent(c_UserChannel_HintsBackground);
    ImGui::PopClipRect();

    Editor->GetDrawList()->ChannelsSetCurrent(m_LastChannel);

    Editor->Resume(SuspendFlags::KeepSplitter);

    m_IsActive    = false;
    m_CurrentNode = nullptr;
}

ImVec2 ed::HintBuilder::GetGroupMin()
{
    IM_ASSERT(nullptr != m_CurrentNode);

    return Editor->ToScreen(m_CurrentNode->m_Bounds.Min);
}

ImVec2 ed::HintBuilder::GetGroupMax()
{
    IM_ASSERT(nullptr != m_CurrentNode);

    return Editor->ToScreen(m_CurrentNode->m_Bounds.Max);
}

ImDrawList* ed::HintBuilder::GetForegroundDrawList()
{
    IM_ASSERT(nullptr != m_CurrentNode);

    auto drawList = Editor->GetDrawList();

    drawList->ChannelsSetCurrent(c_UserChannel_Hints);

    return drawList;
}

ImDrawList* ed::HintBuilder::GetBackgroundDrawList()
{
    IM_ASSERT(nullptr != m_CurrentNode);

    auto drawList = Editor->GetDrawList();

    drawList->ChannelsSetCurrent(c_UserChannel_HintsBackground);

    return drawList;
}




//------------------------------------------------------------------------------
//
// Style
//
//------------------------------------------------------------------------------
void ed::Style::PushColor(StyleColor colorIndex, const ImVec4& color)
{
    ColorModifier modifier;
    modifier.Index = colorIndex;
    modifier.Value = Colors[colorIndex];
    m_ColorStack.push_back(modifier);
    Colors[colorIndex] = color;
}

void ed::Style::PopColor(int count)
{
    while (count > 0)
    {
        auto& modifier = m_ColorStack.back();
        Colors[modifier.Index] = modifier.Value;
        m_ColorStack.pop_back();
        --count;
    }
}

void ed::Style::PushVar(StyleVar varIndex, float value)
{
    auto* var = GetVarFloatAddr(varIndex);
    IM_ASSERT(var != nullptr);
    VarModifier modifier;
    modifier.Index = varIndex;
    modifier.Value = ImVec4(*var, 0, 0, 0);
    *var = value;
    m_VarStack.push_back(modifier);
}

void ed::Style::PushVar(StyleVar varIndex, const ImVec2& value)
{
    auto* var = GetVarVec2Addr(varIndex);
    IM_ASSERT(var != nullptr);
    VarModifier modifier;
    modifier.Index = varIndex;
    modifier.Value = ImVec4(var->x, var->y, 0, 0);
    *var = value;
    m_VarStack.push_back(modifier);
}

void ed::Style::PushVar(StyleVar varIndex, const ImVec4& value)
{
    auto* var = GetVarVec4Addr(varIndex);
    IM_ASSERT(var != nullptr);
    VarModifier modifier;
    modifier.Index = varIndex;
    modifier.Value = *var;
    *var = value;
    m_VarStack.push_back(modifier);
}

void ed::Style::PopVar(int count)
{
    while (count > 0)
    {
        auto& modifier = m_VarStack.back();
        if (auto floatValue = GetVarFloatAddr(modifier.Index))
            *floatValue = modifier.Value.x;
        else if (auto vec2Value = GetVarVec2Addr(modifier.Index))
            *vec2Value = ImVec2(modifier.Value.x, modifier.Value.y);
        else if (auto vec4Value = GetVarVec4Addr(modifier.Index))
            *vec4Value = modifier.Value;
        m_VarStack.pop_back();
        --count;
    }
}

const char* ed::Style::GetColorName(StyleColor colorIndex) const
{
    switch (colorIndex)
    {
        case StyleColor_Bg: return "Bg";
        case StyleColor_Grid: return "Grid";
        case StyleColor_NodeBg: return "NodeBg";
        case StyleColor_NodeBorder: return "NodeBorder";
        case StyleColor_HovNodeBorder: return "HoveredNodeBorder";
        case StyleColor_SelNodeBorder: return "SelNodeBorder";
        case StyleColor_NodeSelRect: return "NodeSelRect";
        case StyleColor_NodeSelRectBorder: return "NodeSelRectBorder";
        case StyleColor_HovLinkBorder: return "HoveredLinkBorder";
        case StyleColor_SelLinkBorder: return "SelLinkBorder";
        case StyleColor_HighlightLinkBorder: return "HighlightLinkBorder";
        case StyleColor_LinkSelRect: return "LinkSelRect";
        case StyleColor_LinkSelRectBorder: return "LinkSelRectBorder";
        case StyleColor_PinRect: return "PinRect";
        case StyleColor_PinRectBorder: return "PinRectBorder";
        case StyleColor_Flow: return "Flow";
        case StyleColor_FlowMarker: return "FlowMarker";
        case StyleColor_GroupBg: return "GroupBg";
        case StyleColor_GroupBorder: return "GroupBorder";
        case StyleColor_Count: break;
    }

    IM_ASSERT(0);
    return "Unknown";
}

float* ed::Style::GetVarFloatAddr(StyleVar idx)
{
    switch (idx)
    {
        case StyleVar_NodeRounding:             return &NodeRounding;
        case StyleVar_NodeBorderWidth:          return &NodeBorderWidth;
        case StyleVar_HoveredNodeBorderWidth:   return &HoveredNodeBorderWidth;
        case StyleVar_SelectedNodeBorderWidth:  return &SelectedNodeBorderWidth;
        case StyleVar_PinRounding:              return &PinRounding;
        case StyleVar_PinBorderWidth:           return &PinBorderWidth;
        case StyleVar_LinkStrength:             return &LinkStrength;
        case StyleVar_ScrollDuration:           return &ScrollDuration;
        case StyleVar_FlowMarkerDistance:       return &FlowMarkerDistance;
        case StyleVar_FlowSpeed:                return &FlowSpeed;
        case StyleVar_FlowDuration:             return &FlowDuration;
        case StyleVar_PinCorners:               return &PinCorners;
        case StyleVar_PinRadius:                return &PinRadius;
        case StyleVar_PinArrowSize:             return &PinArrowSize;
        case StyleVar_PinArrowWidth:            return &PinArrowWidth;
        case StyleVar_GroupRounding:            return &GroupRounding;
        case StyleVar_GroupBorderWidth:         return &GroupBorderWidth;
        case StyleVar_HighlightConnectedLinks:  return &HighlightConnectedLinks;
        case StyleVar_SnapLinkToPinDir:         return &SnapLinkToPinDir;
        case StyleVar_HoveredNodeBorderOffset:  return &HoverNodeBorderOffset;
        case StyleVar_SelectedNodeBorderOffset: return &SelectedNodeBorderOffset;
        default:                                return nullptr;
    }
}

ImVec2* ed::Style::GetVarVec2Addr(StyleVar idx)
{
    switch (idx)
    {
        case StyleVar_SourceDirection:  return &SourceDirection;
        case StyleVar_TargetDirection:  return &TargetDirection;
        case StyleVar_PivotAlignment:   return &PivotAlignment;
        case StyleVar_PivotSize:        return &PivotSize;
        case StyleVar_PivotScale:       return &PivotScale;
        default:                        return nullptr;
    }
}

ImVec4* ed::Style::GetVarVec4Addr(StyleVar idx)
{
    switch (idx)
    {
        case StyleVar_NodePadding:  return &NodePadding;
        default:                    return nullptr;
    }
}




//------------------------------------------------------------------------------
//
// Config
//
//------------------------------------------------------------------------------
ed::Config::Config(const ax::NodeEditor::Config* config)
{
    if (config)
    {
        *static_cast<ax::NodeEditor::Config*>(this) = *config;
        // Explicitly copy new callback fields
        GetContainerNodeIds = config->GetContainerNodeIds;
        GetContainerLinkIds = config->GetContainerLinkIds;
    }
    else
    {
        GetContainerNodeIds = nullptr;
        GetContainerLinkIds = nullptr;
    }
}

std::string ed::Config::Load()
{
    std::string data;

    if (LoadSettings)
    {
        const auto size = LoadSettings(nullptr, UserPointer);
        if (size > 0)
        {
            data.resize(size);
            LoadSettings(const_cast<char*>(data.data()), UserPointer);
        }
    }
    else if (SettingsFile)
    {
        std::ifstream file(SettingsFile);
        if (file)
        {
            file.seekg(0, std::ios_base::end);
            auto size = static_cast<size_t>(file.tellg());
            file.seekg(0, std::ios_base::beg);

            data.reserve(size);
            data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
    }

    return data;
}

std::string ed::Config::LoadNode(NodeId nodeId)
{
    std::string data;

    if (LoadNodeSettings)
    {
        const auto size = LoadNodeSettings(nodeId, nullptr, UserPointer);
        if (size > 0)
        {
            data.resize(size);
            LoadNodeSettings(nodeId, const_cast<char*>(data.data()), UserPointer);
        }
    }

    return data;
}

void ed::Config::BeginSave()
{
    if (BeginSaveSession)
        BeginSaveSession(UserPointer);
}

bool ed::Config::Save(const std::string& data, SaveReasonFlags flags)
{
    if (SaveSettings)
    {
        return SaveSettings(data.c_str(), data.size(), flags, UserPointer);
    }
    else if (SettingsFile)
    {
        std::ofstream settingsFile(SettingsFile);
        if (settingsFile)
            settingsFile << data;

        return !!settingsFile;
    }

    return false;
}

bool ed::Config::SaveNode(NodeId nodeId, const std::string& data, SaveReasonFlags flags)
{
    if (SaveNodeSettings)
        return SaveNodeSettings(nodeId, data.c_str(), data.size(), flags, UserPointer);

    return false;
}

void ed::Config::EndSave()
{
    if (EndSaveSession)
        EndSaveSession(UserPointer);
}

} // namespace Detail
} // namespace NodeEditor
} // namespace ax
