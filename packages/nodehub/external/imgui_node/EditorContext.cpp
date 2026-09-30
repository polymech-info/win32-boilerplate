//------------------------------------------------------------------------------
// EditorContext.cpp
// Implementation file for EditorContext class
// Consolidated from imgui_node_editor.cpp, imgui_node_editor_render.cpp,
// imgui_node_editor_tools.cpp, imgui_node_editor_selection.cpp,
// imgui_node_editor_links.cpp, imgui_node_editor_animation.cpp,
// imgui_node_editor_runtime.cpp, and imgui_node_editor_store.cpp
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
# include <cstdarg>
# include <vector>

namespace ed = ax::NodeEditor::Detail;

ed::EditorContext::EditorContext(const ax::NodeEditor::Config* config)
    : m_Config(config)
    , m_EditorActiveId(0)
    , m_IsFirstFrame(true)
    , m_IsFocused(false)
    , m_IsHovered(false)
    , m_IsHoveredWithoutOverlapp(false)
    , m_ShortcutsEnabled(true)
    , m_Style()
    , m_Nodes()
    , m_Pins()
    , m_Links()
    , m_SelectionId(1)
    , m_LastActiveLink(nullptr)
    , m_Canvas()
    , m_IsCanvasVisible(false)
    , m_NodeBuilder(this)
    , m_HintBuilder(this)
    , m_CurrentAction(nullptr)
    , m_NavigateAction(this, m_Canvas)
    , m_SizeAction(this)
    , m_DragAction(this)
    , m_SelectAction(this)
    , m_ContextMenuAction(this)
    , m_ShortcutAction(this)
    , m_CreateItemAction(this)
    , m_DeleteItemsAction(this)
    , m_AnimationControllers{ &m_FlowAnimationController }
    , m_FlowAnimationController(this)
    , m_HoveredNode(0)
    , m_HoveredPin(0)
    , m_HoveredLink(0)
    , m_DoubleClickedNode(0)
    , m_DoubleClickedPin(0)
    , m_DoubleClickedLink(0)
    , m_BackgroundClickButtonIndex(-1)
    , m_BackgroundDoubleClickButtonIndex(-1)
    , m_EditingLink(nullptr)
    , m_DraggingControlPointIndex(-1)
    , m_ControlPointDragStart(0, 0)
    , m_GuidedLinks()
    , m_IsInitialized(false)
    , m_Settings()
    , m_DrawList(nullptr)
    , m_ExternalChannel(0)
{
    
}

ed::EditorContext::~EditorContext()
{
    Log("=== ImGui Node Editor Context Destroyed ===");
    
    if (m_IsInitialized)
        SaveSettings();
    
    // Clean up guided link data
    Log("Cleaning up %zu guided links", m_GuidedLinks.size());
    for (auto& pair : m_GuidedLinks)
        delete pair.second;
    m_GuidedLinks.clear();

    for (auto link  : m_Links)  delete link.m_Object;
    for (auto pin   : m_Pins)   delete pin.m_Object;
    for (auto node  : m_Nodes)  delete node.m_Object;

    m_Splitter.ClearFreeMemory();
}

void ed::EditorContext::Begin(const char* id, const ImVec2& size)
{
    m_EditorActiveId = ImGui::GetID(id);
    ImGui::PushID(id);

    auto availableContentSize = ImGui::GetContentRegionAvail();
    ImVec2 canvasSize = ImFloor(size);
    if (canvasSize.x <= 0.0f)
        canvasSize.x = ImMax(4.0f, availableContentSize.x);
    if (canvasSize.y <= 0.0f)
        canvasSize.y = ImMax(4.0f, availableContentSize.y);

    if (!m_IsInitialized)
    {
        // Cycle canvas, so it has a chance to initialize its size before settings are loaded
        if (m_Canvas.Begin(id, canvasSize))
            m_Canvas.End();

        LoadSettings();
        m_IsInitialized = true;
    }

    static auto resetAndCollect = [](auto& objects)
    {
        objects.erase(std::remove_if(objects.begin(), objects.end(), [](auto objectWrapper)
        {
            if (objectWrapper->m_DeleteOnNewFrame)
            {
                delete objectWrapper.m_Object;
                return true;
            }
            else
            {
                objectWrapper->Reset();
                return false;
            }
        }), objects.end());
    };

    resetAndCollect(m_Nodes);
    resetAndCollect(m_Pins);
    resetAndCollect(m_Links);

    m_DrawList = ImGui::GetWindowDrawList();

    ImDrawList_SwapSplitter(m_DrawList, m_Splitter);
    m_ExternalChannel = m_DrawList->_Splitter._Current;

    if (m_CurrentAction && m_CurrentAction->IsDragging() && m_NavigateAction.MoveOverEdge(canvasSize))
    {
        auto& io = ImGui::GetIO();
        auto offset = m_NavigateAction.GetMoveScreenOffset();
        for (int i = 0; i < 5; ++i)
            io.MouseClickedPos[i] = io.MouseClickedPos[i] - offset;
    }
    else
        m_NavigateAction.StopMoveOverEdge();

    auto previousSize        = m_Canvas.Rect().GetSize();
    auto previousVisibleRect = m_Canvas.ViewRect();
    m_IsCanvasVisible = m_Canvas.Begin(id, canvasSize);

    m_IsFocused = ImGui::IsWindowFocused();

    m_NavigateAction.SetWindow(m_Canvas.ViewRect().Min, m_Canvas.ViewRect().GetSize());

    // Handle canvas size change. Scale to Y axis, center on X.
    if (!ImRect_IsEmpty(previousVisibleRect) && previousSize != canvasSize)
    {
        m_NavigateAction.FinishNavigation();

        auto centerX            = (previousVisibleRect.Max.x + previousVisibleRect.Min.x) * 0.5f;
        auto centerY            = (previousVisibleRect.Max.y + previousVisibleRect.Min.y) * 0.5f;
        auto currentVisibleRect = m_Canvas.ViewRect();
        auto currentAspectRatio = currentVisibleRect.GetHeight() ? (currentVisibleRect.GetWidth() / currentVisibleRect.GetHeight()) : 0.0f;
        auto width              = previousVisibleRect.GetWidth();
        auto height             = previousVisibleRect.GetHeight();

        if (m_Config.CanvasSizeMode == ax::NodeEditor::CanvasSizeMode::FitVerticalView)
        {
            height  = previousVisibleRect.GetHeight();
            width   = height * currentAspectRatio;
        }
        else if (m_Config.CanvasSizeMode == ax::NodeEditor::CanvasSizeMode::FitHorizontalView)
        {
            width   = previousVisibleRect.GetWidth();
            height  = width / currentAspectRatio;
        }
        else if (m_Config.CanvasSizeMode == ax::NodeEditor::CanvasSizeMode::CenterOnly)
        {
            width  = currentVisibleRect.GetWidth();
            height = currentVisibleRect.GetHeight();
        }

        previousVisibleRect.Min.x = centerX - 0.5f * width;
        previousVisibleRect.Max.x = centerX + 0.5f * width;
        previousVisibleRect.Min.y = centerY - 0.5f * height;
        previousVisibleRect.Max.y = centerY + 0.5f * height;

        m_NavigateAction.NavigateTo(previousVisibleRect, Detail::NavigateAction::ZoomMode::Exact, 0.0f);
    }

    m_Canvas.SetView(m_NavigateAction.GetView());

    // Reserve channels for background and links
    ImDrawList_ChannelsGrow(m_DrawList, c_NodeStartChannel);

    if (HasSelectionChanged())
        ++m_SelectionId;

    m_LastSelectedObjects = m_SelectedObjects;
}

bool ed::EditorContext::DoLink(LinkId id, PinId startPinId, PinId endPinId, ImU32 color, float thickness)
{
    auto startPin = FindPin(startPinId);
    auto endPin   = FindPin(endPinId);

    if (!startPin || !startPin->m_IsLive || !endPin || !endPin->m_IsLive)
    {
        return false;
    }

    startPin->m_HasConnection = true;
    endPin->m_HasConnection = true;

    auto link           = GetLink(id);
    link->m_StartPin      = startPin;
    link->m_EndPin        = endPin;
    link->m_Color         = color;
    link->m_HighlightColor= GetColor(StyleColor_HighlightLinkBorder);
    link->m_Thickness     = thickness;
    link->m_IsLive        = true;

    link->UpdateEndpoints();
    
    // Apply guided link settings if they exist
    if (!link->m_GuidedData)
    {
        auto linkSettings = m_Settings.FindLink(id);
        if (linkSettings && linkSettings->m_Mode != LinkMode::Auto)
        {
            auto guidedData = CreateGuidedLinkData(id);
            if (guidedData)
            {
                guidedData->Mode = linkSettings->m_Mode;
                guidedData->EnableSnapping = linkSettings->m_EnableSnapping;
                guidedData->SnapGridSize = linkSettings->m_SnapGridSize;
                guidedData->ControlPoints.clear();
                
                if (linkSettings->m_Mode == LinkMode::Guided)
                {
                    for (const auto& pt : linkSettings->m_ControlPoints)
                    {
                        const float MAX_VALID_COORD = 1000000.0f;
                        if (std::isfinite(pt.x) && std::isfinite(pt.y) && 
                            std::abs(pt.x) < MAX_VALID_COORD && std::abs(pt.y) < MAX_VALID_COORD)
                        {
                            guidedData->AddControlPoint(pt);
                        }
                        else
                        {
                            Log("  Skipping invalid control point at (%.1f, %.1f)", pt.x, pt.y);
                        }
                    }
                }
                
                link->m_GuidedData = guidedData;
                
                const float MAX_VALID_COORD = 1000000.0f;
                auto& points = link->m_GuidedData->ControlPoints;
                points.erase(
                    std::remove_if(points.begin(), points.end(),
                        [MAX_VALID_COORD](const auto& cp) {
                            return !std::isfinite(cp.Position.x) || !std::isfinite(cp.Position.y) ||
                                   std::abs(cp.Position.x) >= MAX_VALID_COORD || 
                                   std::abs(cp.Position.y) >= MAX_VALID_COORD;
                        }),
                    points.end());
                
                if (points.empty() && link->m_GuidedData->Mode == LinkMode::Guided)
                {
                    Log("  All control points invalid for link %p, reverting to Auto mode", id.AsPointer());
                    link->m_GuidedData->Mode = LinkMode::Auto;
                }
            }
        }
    }

    return true;
}

void ed::EditorContext::MarkNodeToRestoreState(Node* node)
{
    node->m_RestoreState = true;
}

void ed::EditorContext::UpdateNodeState(Node* node)
{
    bool tryLoadState = node->m_RestoreState;

    node->m_RestoreState = false;

    auto settings = m_Settings.FindNode(node->m_ID);
    if (!settings)
        return;

    if (!tryLoadState && settings->m_WasUsed)
        return;

    if (!settings->m_WasUsed)
    {
        MakeDirty(SaveReasonFlags::AddNode, node);
        settings->m_WasUsed = true;
    }

    if (tryLoadState)
    {
        NodeSettings newSettings = *settings;
        if (NodeSettings::Parse(m_Config.LoadNode(node->m_ID), newSettings))
            *settings = newSettings;
    }

    node->m_Bounds.Min      = settings->m_Location;
    node->m_Bounds.Max      = node->m_Bounds.Min + settings->m_Size;
    node->m_Bounds.Floor();
    node->m_GroupBounds.Min = settings->m_Location;
    node->m_GroupBounds.Max = node->m_GroupBounds.Min + settings->m_GroupSize;
    node->m_GroupBounds.Floor();
}

void ed::EditorContext::RemoveSettings(Object* object)
{
    if (auto node = object->AsNode())
    {
        m_Settings.RemoveNode(node->m_ID);
        MakeDirty(SaveReasonFlags::RemoveNode, node);
    }
}

void ed::EditorContext::Suspend(SuspendFlags flags)
{
    IM_ASSERT(m_DrawList != nullptr && "Suspend was called outiside of Begin/End.");
    auto lastChannel = m_DrawList->_Splitter._Current;
    m_DrawList->ChannelsSetCurrent(m_ExternalChannel);
    if (m_IsCanvasVisible)
        m_Canvas.Suspend();
    m_DrawList->ChannelsSetCurrent(lastChannel);
    if ((flags & SuspendFlags::KeepSplitter) != SuspendFlags::KeepSplitter)
        ImDrawList_SwapSplitter(m_DrawList, m_Splitter);
}

void ed::EditorContext::Resume(SuspendFlags flags)
{
    IM_ASSERT(m_DrawList != nullptr && "Reasume was called outiside of Begin/End.");
    if ((flags & SuspendFlags::KeepSplitter) != SuspendFlags::KeepSplitter)
        ImDrawList_SwapSplitter(m_DrawList, m_Splitter);
    auto lastChannel = m_DrawList->_Splitter._Current;
    m_DrawList->ChannelsSetCurrent(m_ExternalChannel);
    if (m_IsCanvasVisible)
        m_Canvas.Resume();
    m_DrawList->ChannelsSetCurrent(lastChannel);
}

bool ed::EditorContext::IsSuspended()
{
    return m_Canvas.IsSuspended();
}

bool ed::EditorContext::IsFocused()
{
    return m_IsFocused;
}

bool ed::EditorContext::IsHovered() const
{
    return m_IsHovered;
}

bool ed::EditorContext::IsHoveredWithoutOverlapp() const
{
    return m_IsHoveredWithoutOverlapp;
}

bool ed::EditorContext::CanAcceptUserInput() const
{
    return m_IsFocused && m_IsHovered;
}

int ed::EditorContext::CountLiveNodes() const
{
    return (int)std::count_if(m_Nodes.begin(),  m_Nodes.end(),  [](const Node* node)  { return node->m_IsLive; });
}

int ed::EditorContext::CountLivePins() const
{
    return (int)std::count_if(m_Pins.begin(),   m_Pins.end(),   [](const Pin*  pin)   { return pin->m_IsLive; });
}

int ed::EditorContext::CountLiveLinks() const
{
    return (int)std::count_if(m_Links.begin(),  m_Links.end(),  [](const Link* link)  { return link->m_IsLive; });
}

ed::Pin* ed::EditorContext::CreatePin(PinId id, PinKind kind)
{
    IM_ASSERT(nullptr == FindObject(id));
    auto pin = new Pin(this, id, kind);
    m_Pins.push_back({id, pin});
    std::sort(m_Pins.begin(), m_Pins.end());
    return pin;
}

ed::Node* ed::EditorContext::CreateNode(NodeId id)
{
    IM_ASSERT(nullptr == FindObject(id));
    auto node = new Node(this, id);
    m_Nodes.push_back({id, node});

    auto settings = m_Settings.FindNode(id);
    if (!settings)
    {
        settings = m_Settings.AddNode(id);
    }

    UpdateNodeState(node);

    if (settings->m_GroupSize.x > 0 || settings->m_GroupSize.y > 0)
        node->m_Type = NodeType::Group;

    node->m_IsLive = false;

    return node;
}

ImU32 ed::EditorContext::GetColor(StyleColor colorIndex) const
{
    return ImColor(m_Style.Colors[colorIndex]);
}

ImU32 ed::EditorContext::GetColor(StyleColor colorIndex, float alpha) const
{
    auto color = m_Style.Colors[colorIndex];
    return ImColor(color.x, color.y, color.z, color.w * alpha);
}

void ed::EditorContext::SetUserContext(bool globalSpace)
{
    const auto mousePos = ImGui::GetMousePos();

    if (globalSpace)
        ImGui::SetCursorScreenPos(m_Canvas.FromLocal(mousePos));
    else
        ImGui::SetCursorScreenPos(m_Canvas.FromLocal(mousePos));

    if (!IsSuspended())
    {
        m_DrawList->ChannelsSetCurrent(c_UserChannel_Content);
    }
}

void ed::EditorContext::EnableShortcuts(bool enable)
{
    m_ShortcutsEnabled = enable;
}

bool ed::EditorContext::AreShortcutsEnabled()
{
    return m_ShortcutsEnabled;
}

ed::Control ed::EditorContext::BuildControl(bool allowOffscreen)
{
    m_IsHovered = false;
    m_IsHoveredWithoutOverlapp = false;

    const auto windowHovered = ImGui::IsWindowHovered();
    const auto widgetHovered = ImGui::IsMouseHoveringRect(m_Canvas.ViewRect().Min, m_Canvas.ViewRect().Max, true);

    if (!allowOffscreen && !windowHovered && !widgetHovered)
        return Control();

    const auto mousePos = ImGui::GetMousePos();

    auto editorRect = m_Canvas.ViewRect();
    auto isMouseOffscreen = allowOffscreen && !editorRect.Contains(mousePos);
    if (isMouseOffscreen)
    {
        editorRect.Add(ImFloor(mousePos));
        editorRect.Add(ImVec2(ImCeil(mousePos.x), ImCeil(mousePos.y)));

        ImGui::PushClipRect(editorRect.Min, editorRect.Max, false);
    }

    ImGuiID activeId            = 0;
    Object* hotObject           = nullptr;
    Object* activeObject        = nullptr;
    Object* clickedObject       = nullptr;
    Object* doubleClickedObject = nullptr;

    ImGuiButtonFlags extraFlags = ImGuiButtonFlags_None;
    extraFlags |= ImGuiButtonFlags_MouseButtonLeft;
    extraFlags |= ImGuiButtonFlags_MouseButtonRight;
    extraFlags |= ImGuiButtonFlags_MouseButtonMiddle;

    static auto invisibleButtonEx = [](const char* str_id, const ImVec2& size_arg, ImGuiButtonFlags extraFlags) -> int
    {
        using namespace ImGui;

        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return -1;

        if (size_arg.x == 0.0f || size_arg.y == 0.0f)
            return false;

        const ImGuiID id = window->GetID(str_id);
        ImVec2 size = CalcItemSize(size_arg, 0.0f, 0.0f);
        const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
        ItemSize(size);
        if (!ItemAdd(bb, id))
            return -1;

        auto buttonIndex = ImGui::GetCurrentContext()->ActiveIdMouseButton;

        bool hovered, held;
        bool pressed = ButtonBehavior(bb, id, &hovered, &held, extraFlags);

        if (pressed && buttonIndex >= 0 && buttonIndex < ImGuiMouseButton_COUNT)
            return buttonIndex;
        else if (pressed)
            return 0;
        else
            return -1;
    };

    auto emitInteractiveAreaEx = [&activeId](ObjectId id, const ImRect& rect, ImGuiButtonFlags extraFlags) -> int
    {
        char idString[33] = { 0 };
        snprintf(idString, 32, "%p", id.AsPointer());
        ImGui::SetCursorScreenPos(rect.Min);

        auto buttonIndex = invisibleButtonEx(idString, rect.GetSize(), extraFlags);

        if (ImGui::IsItemActive())
            activeId = ImGui::GetActiveID();

        return buttonIndex;
    };

    auto emitInteractiveArea = [&emitInteractiveAreaEx, extraFlags](ObjectId id, const ImRect& rect)
    {
        return emitInteractiveAreaEx(id, rect, extraFlags);
    };

    auto checkInteractionsInArea = [this, &emitInteractiveArea, &hotObject, &activeObject, &clickedObject, &doubleClickedObject](ObjectId id, const ImRect& rect, Object* object) -> bool
    {
        auto buttonIndex = emitInteractiveArea(id, rect);
        bool isClick = (buttonIndex >= 0);
        bool isDragging = ImGui::IsMouseDragging(0, 1) || ImGui::IsMouseDragging(1, 1) || ImGui::IsMouseDragging(2, 1);
        
        if (isClick && !isDragging)
        {
            const char* objType = "Unknown";
            void* objId = nullptr;
            if (auto node = object->AsNode())
            {
                objType = "Node";
                objId = node->m_ID.AsPointer();
            }
            else if (auto pin = object->AsPin())
            {
                objType = "Pin";
                objId = pin->m_ID.AsPointer();                
            }
            else if (auto link = object->AsLink())
            {
                objType = "Link";
                objId = link->m_ID.AsPointer();
            }
            
            if (buttonIndex >= 0)
            {
                Log("BuildControl: Setting clickedObject to %s %p (prev was %s)", 
                    objType, objId, 
                    clickedObject ? (clickedObject->AsNode() ? "Node" : clickedObject->AsPin() ? "Pin" : clickedObject->AsLink() ? "Link" : "Unknown") : "null");
            }
        }
        
        if (buttonIndex >= 0)
            clickedObject = object;
        if (!doubleClickedObject && ImGui::IsMouseDoubleClicked(m_Config.DragButtonIndex) && ImGui::IsItemHovered())
            doubleClickedObject = object;

        if (!hotObject && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            hotObject = object;

        bool isActive = ImGui::IsItemActive();
        if (isActive)
        {
            if (auto pin = object->AsPin())
            {
                auto activeNode = activeObject ? activeObject->AsNode() : nullptr;
                if (activeNode && pin->m_Node != activeNode)
                {
                    return false;
                }
            }
            
            activeObject = object;
            return true;
        }
        return false;
    };

    std::vector<Node*> nodesToProcess;
    
    if (m_Config.GetContainerNodeIds && m_Config.UserPointer)
    {
        int nodeCount = m_Config.GetContainerNodeIds(m_Config.UserPointer, nullptr, 0);
        
        if (nodeCount > 0)
        {
            std::vector<NodeId> nodeIds(nodeCount);
            
            int actualCount = m_Config.GetContainerNodeIds(m_Config.UserPointer, nodeIds.data(), nodeCount);
            
            for (int i = 0; i < actualCount; ++i)
            {
                NodeId nodeId = nodeIds[i];
                if (!nodeId) continue;
                
                Node* editorNode = GetNode(nodeId);
                if (!editorNode) continue;
                
                if (!editorNode->m_IsLive)
                {
                    continue;
                }
                
                UpdateNodeState(editorNode);
                
                const ImRect& bounds = editorNode->m_Bounds;
                if (!std::isfinite(bounds.Min.x) || !std::isfinite(bounds.Min.y) ||
                    !std::isfinite(bounds.Max.x) || !std::isfinite(bounds.Max.y) ||
                    bounds.Min.x > bounds.Max.x || bounds.Min.y > bounds.Max.y)
                {
                    continue;
                }
                
                nodesToProcess.push_back(editorNode);
            }
            
            std::stable_sort(nodesToProcess.begin(), nodesToProcess.end(), 
                [](const Node* lhs, const Node* rhs) {
                    return lhs->m_ZPosition < rhs->m_ZPosition;
                });
        }
    }
    
    if (nodesToProcess.empty())
    {
        for (auto nodeIt = m_Nodes.rbegin(), nodeItEnd = m_Nodes.rend(); nodeIt != nodeItEnd; ++nodeIt)
        {
            auto node = *nodeIt;
            if (node->m_IsLive)
                nodesToProcess.push_back(node);
        }
    }

    for (auto nodeIt = nodesToProcess.rbegin(), nodeItEnd = nodesToProcess.rend(); nodeIt != nodeItEnd; ++nodeIt)
    {
        auto node = *nodeIt;

        if (!node->m_IsLive) continue;

        for (auto pin = node->m_LastPin; pin; pin = pin->m_PreviousPin)
        {
            if (!pin->m_IsLive) continue;

            checkInteractionsInArea(pin->m_ID, pin->m_Bounds, pin);
        }

        if (node->m_Type == NodeType::Group)
        {
            ImGui::PushID(node->m_ID.AsPointer());

            static const NodeRegion c_Regions[] =
            {
                NodeRegion::TopLeft,
                NodeRegion::TopRight,
                NodeRegion::BottomLeft,
                NodeRegion::BottomRight,
                NodeRegion::Top,
                NodeRegion::Bottom,
                NodeRegion::Left,
                NodeRegion::Right,
                NodeRegion::Header,
            };

            for (auto region : c_Regions)
            {
                auto bounds = node->GetRegionBounds(region);
                if (ImRect_IsEmpty(bounds))
                    continue;
                checkInteractionsInArea(NodeId(static_cast<int>(region)), bounds, node);
            }

            ImGui::PopID();
        }
        else
        {
            checkInteractionsInArea(node->m_ID, node->m_Bounds, node);
        }
    }

    if (nullptr == hotObject)
        hotObject = FindLinkAt(mousePos);

    ImGuiButtonFlags backgroundExtraFlags = ImGuiButtonFlags_None;
    if (m_Config.DragButtonIndex == 0 || m_Config.SelectButtonIndex == 0 || m_Config.NavigateButtonIndex == 0)
        backgroundExtraFlags |= ImGuiButtonFlags_MouseButtonLeft;
    if (m_Config.DragButtonIndex == 1 || m_Config.SelectButtonIndex == 1 || m_Config.NavigateButtonIndex == 1)
        backgroundExtraFlags |= ImGuiButtonFlags_MouseButtonRight;
    if (m_Config.DragButtonIndex == 2 || m_Config.SelectButtonIndex == 2 || m_Config.NavigateButtonIndex == 2)
        backgroundExtraFlags |= ImGuiButtonFlags_MouseButtonMiddle;

    auto isMouseDoubleClickOverBackground = [doubleClickedObject, backgroundExtraFlags]() -> int
    {
        if (doubleClickedObject)
            return -1;

        if (!ImGui::IsItemHovered())
            return -1;

        if ((backgroundExtraFlags & ImGuiButtonFlags_MouseButtonLeft) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            return ImGuiButtonFlags_MouseButtonLeft;
        if ((backgroundExtraFlags & ImGuiButtonFlags_MouseButtonRight) && ImGui::IsMouseDoubleClicked(ImGuiButtonFlags_MouseButtonRight))
            return ImGuiButtonFlags_MouseButtonRight;
        if ((backgroundExtraFlags & ImGuiButtonFlags_MouseButtonMiddle) && ImGui::IsMouseDoubleClicked(ImGuiButtonFlags_MouseButtonMiddle))
            return ImGuiButtonFlags_MouseButtonMiddle;

        return -1;
    };

    auto backgroundClickButonIndex        = emitInteractiveAreaEx(NodeId(0), editorRect, backgroundExtraFlags);
    auto backgroundDoubleClickButtonIndex = isMouseDoubleClickOverBackground();
    auto isBackgroundActive               = ImGui::IsItemActive();
    auto isBackgroundHot                  = !hotObject;
    auto isDragging                       = ImGui::IsMouseDragging(0, 1) || ImGui::IsMouseDragging(1, 1) || ImGui::IsMouseDragging(2, 1);

    if (backgroundDoubleClickButtonIndex >= 0)
        backgroundClickButonIndex = -1;

    if (isMouseOffscreen)
        ImGui::PopClipRect();

    auto hotLink = hotObject ? hotObject->AsLink() : nullptr;

    if (!isDragging && isBackgroundActive && hotLink && !m_LastActiveLink)
        m_LastActiveLink = hotLink;
    
    if (isBackgroundActive && m_LastActiveLink)
    {
        activeObject       = m_LastActiveLink;
        isBackgroundActive = false;
    }
    else if (!isBackgroundActive && m_LastActiveLink)
        m_LastActiveLink = nullptr;

    if (!isDragging && backgroundClickButonIndex >= 0 && hotLink)
    {
        clickedObject             = hotLink;
        backgroundClickButonIndex = -1;
    }

    if (!isDragging && backgroundDoubleClickButtonIndex >= 0 && hotLink)
    {
        doubleClickedObject              = hotLink;
        backgroundDoubleClickButtonIndex = -1;
    }

    if (activeId)
        m_EditorActiveId = activeId;

    if (ImGui::IsAnyItemActive() && ImGui::GetActiveID() != m_EditorActiveId)
        return Control();

    m_IsHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);
    m_IsHoveredWithoutOverlapp = ImGui::IsItemHovered();
    if (!allowOffscreen && !m_IsHovered)
        return Control();

# if IMGUI_VERSION_NUM >= 18836
    if (m_IsHoveredWithoutOverlapp)
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
# elif IMGUI_VERSION_NUM >= 17909
    if (m_IsHoveredWithoutOverlapp)
        ImGui::SetItemUsingMouseWheel();
# endif

    if (!isDragging)
    {
        const char* clickedType = "null";
        void* clickedId = nullptr;
        Node* clickedNode = nullptr;
        
        if (clickedObject)
        {
            if (auto node = clickedObject->AsNode())
            {
                clickedType = "Node";
                clickedId = node->m_ID.AsPointer();
                clickedNode = node;
            }
            else if (auto pin = clickedObject->AsPin())
            {
                clickedType = "Pin";
                clickedId = pin->m_ID.AsPointer();
                clickedNode = pin->m_Node;
                Log("BuildControl: Final clickedObject is Pin %p (node %p)", clickedId, clickedNode ? clickedNode->m_ID.AsPointer() : nullptr);
            }
            else if (auto link = clickedObject->AsLink())
            {
                clickedType = "Link";
                clickedId = link->m_ID.AsPointer();
            }
        }

        
        if (clickedNode)
        {
            Log("BuildControl: ClickedNode should be %p (via clickedObject->AsNode())", clickedNode->m_ID.AsPointer());
        }
        else if (clickedObject && !clickedObject->AsNode())
        {
            // Log("BuildControl: WARNING - clickedObject is %s but AsNode() returns null (this is why ClickedNode is null!)", clickedType);
        }
    }

    return Control(hotObject, activeObject, clickedObject, doubleClickedObject,
        isBackgroundHot, isBackgroundActive, backgroundClickButonIndex, backgroundDoubleClickButtonIndex);
}

void ed::EditorContext::ShowMetrics(const Control& control)
{
    auto& io = ImGui::GetIO();

    auto getObjectName = [](Object* object)
    {
        if (!object) return "";
        else if (object->AsNode())  return "Node";
        else if (object->AsPin())   return "Pin";
        else if (object->AsLink())  return "Link";
        else return "";
    };

    auto getHotObjectName = [&control, &getObjectName]()
    {
        if (control.HotObject)
            return getObjectName(control.HotObject);
        else if (control.BackgroundHot)
            return "Background";
        else
            return "<unknown>";
    };

    auto getActiveObjectName = [&control, &getObjectName]()
    {
        if (control.ActiveObject)
            return getObjectName(control.ActiveObject);
        else if (control.BackgroundActive)
            return "Background";
        else
            return "<unknown>";
    };

    auto liveNodeCount  = CountLiveNodes();
    auto livePinCount   = CountLivePins();
    auto liveLinkCount  = CountLiveLinks();

    auto canvasRect     = m_Canvas.Rect();
    auto viewRect       = m_Canvas.ViewRect();
    auto localMousePos  = m_Canvas.ToLocal(io.MousePos);
    auto globalMousePos = io.MousePos;

    ImGui::SetCursorScreenPos(canvasRect.Min + ImVec2(5, 5));
    ImGui::BeginGroup();
    ImGui::Text("Is Focused: %s", m_IsFocused ? "true" : "false");
    ImGui::Text("Is Hovered: %s", m_IsHovered ? "true" : "false");
    ImGui::Text("Is Hovered (without overlapp): %s", m_IsHoveredWithoutOverlapp ? "true" : "false");
    ImGui::Text("Accept Input: %s", CanAcceptUserInput() ? "true" : "false");
    ImGui::Text("View Position: { x=%g y=%g }", viewRect.Min.x, viewRect.Min.y);
    ImGui::Text("View Size: { w=%g h=%g }", viewRect.GetWidth(), viewRect.GetHeight());
    ImGui::Text("Canvas Size: { w=%g h=%g }", canvasRect.GetWidth(), canvasRect.GetHeight());
    ImGui::Text("Mouse: { x=%.0f y=%.0f } global: { x=%g y=%g }", localMousePos.x, localMousePos.y, globalMousePos.x, globalMousePos.y);
    ImGui::Text("Live Nodes: %d", liveNodeCount);
    ImGui::Text("Live Pins: %d", livePinCount);
    ImGui::Text("Live Links: %d", liveLinkCount);
    ImGui::Text("Hot Object: %s (%p)", getHotObjectName(), control.HotObject ? control.HotObject->ID().AsPointer() : nullptr);
    if (auto node = control.HotObject ? control.HotObject->AsNode() : nullptr)
    {
        ImGui::SameLine();
        ImGui::Text("{ x=%g y=%g w=%g h=%g }", node->m_Bounds.Min.x, node->m_Bounds.Min.y, node->m_Bounds.GetWidth(), node->m_Bounds.GetHeight());
    }
    ImGui::Text("Active Object: %s (%p)", getActiveObjectName(), control.ActiveObject ? control.ActiveObject->ID().AsPointer() : nullptr);
    if (auto node = control.ActiveObject ? control.ActiveObject->AsNode() : nullptr)
    {
        ImGui::SameLine();
        ImGui::Text("{ x=%g y=%g w=%g h=%g }", node->m_Bounds.Min.x, node->m_Bounds.Min.y, node->m_Bounds.GetWidth(), node->m_Bounds.GetHeight());
    }
    ImGui::Text("Action: %s", m_CurrentAction ? m_CurrentAction->GetName() : "<none>");
    ImGui::Text("Action Is Dragging: %s", m_CurrentAction && m_CurrentAction->IsDragging() ? "Yes" : "No");
    m_NavigateAction.ShowMetrics();
    m_SizeAction.ShowMetrics();
    m_DragAction.ShowMetrics();
    m_SelectAction.ShowMetrics();
    m_ContextMenuAction.ShowMetrics();
    m_CreateItemAction.ShowMetrics();
    m_DeleteItemsAction.ShowMetrics();
    ImGui::EndGroup();
}

//------------------------------------------------------------------------------
// From imgui_node_editor_render.cpp
//------------------------------------------------------------------------------

void ed::EditorContext::DrawNodes()
{
    for (auto node : m_Nodes)
        if (node->m_IsLive && node->IsVisible())
            node->Draw(m_DrawList);
}

void ed::EditorContext::DrawLinks()
{
    for (auto link : m_Links)
        if (link->m_IsLive && link->IsVisible())
            link->Draw(m_DrawList);
}

void ed::EditorContext::DrawSelectionHighlights(const Control& control)
{
    auto selectedObjects = &m_SelectedObjects;
    if (auto selectAction = m_CurrentAction ? m_CurrentAction->AsSelect() : nullptr)
        selectedObjects = &selectAction->m_CandidateObjects;

    for (auto selectedObject : *selectedObjects)
    {
        if (selectedObject->IsVisible())
            selectedObject->Draw(m_DrawList, Object::Selected);
    }

    static auto isLinkHighlightedForPin = [](const Pin& pin)
    {
        return pin.m_Node->m_HighlightConnectedLinks && pin.m_Node->m_IsSelected;
    };

    for (auto& link : m_Links)
    {
        if (!link->m_IsLive || !link->IsVisible())
            continue;

        auto isLinkHighlighted = isLinkHighlightedForPin(*link->m_StartPin) || isLinkHighlightedForPin(*link->m_EndPin);
        if (!isLinkHighlighted)
            continue;

        link->Draw(m_DrawList, Object::Highlighted);
    }
}

void ed::EditorContext::DrawHoverHighlight(const Control& control, bool isSelecting)
{
    if (isSelecting)
        return;

    auto hoveredObject = control.HotObject;
    if (auto dragAction = m_CurrentAction ? m_CurrentAction->AsDrag() : nullptr)
        hoveredObject = dragAction->m_DraggedObject;
    if (auto sizeAction = m_CurrentAction ? m_CurrentAction->AsSize() : nullptr)
        hoveredObject = sizeAction->m_SizedNode;

    if (hoveredObject && !IsSelected(hoveredObject) && hoveredObject->IsVisible())
        hoveredObject->Draw(m_DrawList, Object::Hovered);
}

void ed::EditorContext::ProcessCurrentAction(const Control& control)
{
    if (m_CurrentAction && !m_CurrentAction->Process(control))
        m_CurrentAction = nullptr;

    if (m_CurrentAction)
        ImGui::SetMouseCursor(m_CurrentAction->GetCursor());
}

void ed::EditorContext::ProcessNavigateAction(const Control& control)
{
    if (m_NavigateAction.m_IsActive)
        m_NavigateAction.Process(control);
    else
        m_NavigateAction.Accept(control);
}

void ed::EditorContext::SelectNextAction(const Control& control)
{
    if (m_CurrentAction)
        return;

    EditorAction* possibleAction = nullptr;

    auto accept = [&possibleAction, &control](EditorAction& action)
    {
        auto result = action.Accept(control);

        if (result == EditorAction::True)
            return true;
        else if (result == EditorAction::Possible) {
            possibleAction = &action;
        }
        else if (result == EditorAction::Possible)
            action.Reject();

        return false;
    };

    if (accept(m_ContextMenuAction))
        m_CurrentAction = &m_ContextMenuAction;
    else if (accept(m_ShortcutAction))
        m_CurrentAction = &m_ShortcutAction;
    else if (accept(m_SizeAction))
        m_CurrentAction = &m_SizeAction;
    else if (accept(m_DragAction))
        m_CurrentAction = &m_DragAction;
    else if (accept(m_CreateItemAction))
        m_CurrentAction = &m_CreateItemAction;
    else if (accept(m_DeleteItemsAction))
        m_CurrentAction = &m_DeleteItemsAction;
    else if (accept(m_SelectAction))
        m_CurrentAction = &m_SelectAction;

    if (control.ActiveObject!= nullptr && m_CurrentAction==nullptr) {
        // m_CurrentAction = &m_SelectAction;
    }

    if (possibleAction)
        ImGui::SetMouseCursor(possibleAction->GetCursor());

    if (m_CurrentAction && possibleAction)
        possibleAction->Reject();
}

bool ed::EditorContext::BringActiveNodeToFront(const Control& control, bool isDragging)
{
    return false;
    if (!control.ActiveNode)
        return false;

    if (!IsGroup(control.ActiveNode))
    {
        auto activeNodeIt = std::find(m_Nodes.begin(), m_Nodes.end(), control.ActiveNode);
        std::rotate(activeNodeIt, activeNodeIt + 1, m_Nodes.end());
        return false;
    }
    else if (!isDragging && m_CurrentAction && m_CurrentAction->AsDrag())
    {
        std::vector<Node*> nodes;
        control.ActiveNode->GetGroupedNodes(nodes);

        std::stable_partition(m_Nodes.begin(), m_Nodes.end(), [&nodes](Node* node)
        {
            return std::find(nodes.begin(), nodes.end(), node) == nodes.end();
        });

        return true;
    }

    return false;
}

void ed::EditorContext::SortNodesByGroupAndZOrder(bool sortGroups)
{
    if (sortGroups || ((m_Settings.m_DirtyReason & (SaveReasonFlags::Position | SaveReasonFlags::Size)) != SaveReasonFlags::None))
    {
        auto groupsItEnd = std::stable_partition(m_Nodes.begin(), m_Nodes.end(), IsGroup);

        std::sort(m_Nodes.begin(), groupsItEnd, [this](Node* lhs, Node* rhs)
        {
            const auto& lhsSize = lhs == m_SizeAction.m_SizedNode ? m_SizeAction.GetStartGroupBounds().GetSize() : lhs->m_GroupBounds.GetSize();
            const auto& rhsSize = rhs == m_SizeAction.m_SizedNode ? m_SizeAction.GetStartGroupBounds().GetSize() : rhs->m_GroupBounds.GetSize();

            const auto lhsArea = lhsSize.x * lhsSize.y;
            const auto rhsArea = rhsSize.x * rhsSize.y;

            return lhsArea > rhsArea;
        });
    }

    std::stable_sort(m_Nodes.begin(), m_Nodes.end(), [](const auto& lhs, const auto& rhs)
    {
        return lhs->m_ZPosition < rhs->m_ZPosition;
    });
}

void ed::EditorContext::ArrangeNodeChannels()
{
    auto liveNodeCount = static_cast<int>(std::count_if(m_Nodes.begin(), m_Nodes.end(), [](Node* node) { return node->m_IsLive; }));
    auto nodeChannelCount = m_DrawList->_Splitter._Count;
    ImDrawList_ChannelsGrow(m_DrawList, m_DrawList->_Splitter._Count + c_ChannelsPerNode * liveNodeCount + c_LinkChannelCount);

    int targetChannel = nodeChannelCount;

    auto copyNode = [this, &targetChannel](Node* node)
    {
        if (!node->m_IsLive)
            return;

        for (int i = 0; i < c_ChannelsPerNode; ++i)
            ImDrawList_SwapChannels(m_DrawList, node->m_Channel + i, targetChannel + i);

        node->m_Channel = targetChannel;
        targetChannel += c_ChannelsPerNode;
    };

    auto groupsItEnd = std::find_if(m_Nodes.begin(), m_Nodes.end(), [](Node* node) { return !IsGroup(node); });

    std::for_each(m_Nodes.begin(), groupsItEnd, copyNode);

    for (int i = 0; i < c_LinkChannelCount; ++i, ++targetChannel)
        ImDrawList_SwapChannels(m_DrawList, c_LinkStartChannel + i, targetChannel);

    std::for_each(groupsItEnd, m_Nodes.end(), copyNode);
}

void ed::EditorContext::DrawGrid()
{
    m_DrawList->ChannelsSetCurrent(c_UserChannel_Grid);

    ImVec2 offset    = m_Canvas.ViewOrigin() * (1.0f / m_Canvas.ViewScale());
    ImU32 GRID_COLOR = GetColor(StyleColor_Grid, ImClamp(m_Canvas.ViewScale() * m_Canvas.ViewScale(), 0.0f, 1.0f));
    float GRID_SX    = 32.0f;
    float GRID_SY    = 32.0f;
    ImVec2 VIEW_POS  = m_Canvas.ViewRect().Min;
    ImVec2 VIEW_SIZE = m_Canvas.ViewRect().GetSize();

    m_DrawList->AddRectFilled(VIEW_POS, VIEW_POS + VIEW_SIZE, GetColor(StyleColor_Bg));

    for (float x = fmodf(offset.x, GRID_SX); x < VIEW_SIZE.x; x += GRID_SX)
        m_DrawList->AddLine(ImVec2(x, 0.0f) + VIEW_POS, ImVec2(x, VIEW_SIZE.y) + VIEW_POS, GRID_COLOR);
    for (float y = fmodf(offset.y, GRID_SY); y < VIEW_SIZE.y; y += GRID_SY)
        m_DrawList->AddLine(ImVec2(0.0f, y) + VIEW_POS, ImVec2(VIEW_SIZE.x, y) + VIEW_POS, GRID_COLOR);
}

void ed::EditorContext::FinalizeDrawChannels()
{
    auto preTransformClipRect = [this](int channelIndex)
    {
        ImDrawChannel& channel = m_DrawList->_Splitter._Channels[channelIndex];
        for (ImDrawCmd& cmd : channel._CmdBuffer)
        {
            auto a = ToCanvas(ImVec2(cmd.ClipRect.x, cmd.ClipRect.y));
            auto b = ToCanvas(ImVec2(cmd.ClipRect.z, cmd.ClipRect.w));
            cmd.ClipRect = ImVec4(a.x, a.y, b.x, b.y);
        }
    };

    m_DrawList->ChannelsSetCurrent(0);

    auto channelCount = m_DrawList->_Splitter._Count;
    ImDrawList_ChannelsGrow(m_DrawList, channelCount + 3);
    ImDrawList_SwapChannels(m_DrawList, c_UserChannel_HintsBackground, channelCount + 0);
    ImDrawList_SwapChannels(m_DrawList, c_UserChannel_Hints,           channelCount + 1);
    ImDrawList_SwapChannels(m_DrawList, c_UserChannel_Content,         channelCount + 2);

    preTransformClipRect(channelCount + 0);
    preTransformClipRect(channelCount + 1);
    preTransformClipRect(channelCount + 2);
}

void ed::EditorContext::MergeChannelsAndFinishCanvas()
{
    m_DrawList->ChannelsMerge();

    if (m_IsCanvasVisible)
        m_Canvas.End();

    ImDrawList_SwapSplitter(m_DrawList, m_Splitter);

    auto& style = ImGui::GetStyle();
    auto borderShadowColor = style.Colors[ImGuiCol_BorderShadow];
    auto borderColor = style.Colors[ImGuiCol_Border];
    m_DrawList->AddRect(m_Canvas.Rect().Min + ImVec2(1, 1), m_Canvas.Rect().Max - ImVec2(1, 1), ImColor(borderShadowColor));
    m_DrawList->AddRect(m_Canvas.Rect().Min, m_Canvas.Rect().Max, ImColor(borderColor));
}

void ed::EditorContext::PostFrameCleanup()
{
    ImGui::PopID();

    if (!m_CurrentAction && m_IsFirstFrame && !m_Settings.m_Selection.empty())
    {
        ClearSelection();
        for (auto id : m_Settings.m_Selection)
            if (auto object = FindObject(id))
                SelectObject(object);
    }

    if (HasSelectionChanged())
        MakeDirty(SaveReasonFlags::Selection);

    if (m_Settings.m_IsDirty && !m_CurrentAction)
        SaveSettings();

    m_DrawList = nullptr;
    m_IsFirstFrame = false;
}

void ed::EditorContext::UpdateControlState(const Control& control)
{
    m_HoveredNode             = control.HotNode && m_CurrentAction == nullptr ? control.HotNode->m_ID : 0;
    m_HoveredPin              = control.HotPin  && m_CurrentAction == nullptr ? control.HotPin->m_ID  : 0;
    m_HoveredLink             = control.HotLink && m_CurrentAction == nullptr ? control.HotLink->m_ID : 0;
    m_DoubleClickedNode       = control.DoubleClickedNode ? control.DoubleClickedNode->m_ID : 0;
    m_DoubleClickedPin        = control.DoubleClickedPin  ? control.DoubleClickedPin->m_ID  : 0;
    m_DoubleClickedLink       = control.DoubleClickedLink ? control.DoubleClickedLink->m_ID : 0;
    m_BackgroundClickButtonIndex       = control.BackgroundClickButtonIndex;
    m_BackgroundDoubleClickButtonIndex = control.BackgroundDoubleClickButtonIndex;
}

void ed::EditorContext::End()
{
    auto control = BuildControl(m_CurrentAction && m_CurrentAction->IsDragging());

    UpdateControlState(control);

    const bool isSelecting = m_CurrentAction && m_CurrentAction->AsSelect() != nullptr;
    const bool isDragging  = m_CurrentAction && m_CurrentAction->AsDrag()   != nullptr;

    DrawNodes();
    DrawLinks();
    DrawSelectionHighlights(control);
    DrawHoverHighlight(control, isSelecting);
    
    HandleControlPointDragging();
    HandleGuidedLinkInteractions(control);
    ShowControlPointHoverCursor(control);
    
    DrawAnimations();
    
    ProcessCurrentAction(control);
    ProcessNavigateAction(control);
    SelectNextAction(control);

    m_SelectAction.Draw(m_DrawList);

    bool sortGroups = BringActiveNodeToFront(control, isDragging);
    SortNodesByGroupAndZOrder(sortGroups);

    ArrangeNodeChannels();
    
    DrawGrid();

    FinalizeDrawChannels();
    UpdateAnimations();
    MergeChannelsAndFinishCanvas();
    
    PostFrameCleanup();
    
    ExecuteRuntime();
}

//------------------------------------------------------------------------------
// From imgui_node_editor_tools.cpp
//------------------------------------------------------------------------------

void ed::EditorContext::SetNodePosition(NodeId nodeId, const ImVec2& position)
{
    auto node = FindNode(nodeId);
    if (!node)
    {
        node = CreateNode(nodeId);
        node->m_IsLive = false;
    }

    if (node->m_Bounds.Min != position)
    {
        node->m_Bounds.Translate(position - node->m_Bounds.Min);
        node->m_Bounds.Floor();
        
        auto settings = m_Settings.FindNode(nodeId);
        if (settings)
        {
            settings->m_Location = node->m_Bounds.Min;
            ImVec2 currentSize = node->m_Bounds.GetSize();
            if (currentSize.x > 0 && currentSize.y > 0)
            {
                settings->m_Size = currentSize;
            }
        }
        
        MakeDirty(NodeEditor::SaveReasonFlags::Position, node);
    }
}

void ed::EditorContext::SetGroupSize(NodeId nodeId, const ImVec2& size)
{
    auto node = FindNode(nodeId);
    if (!node)
    {
        node = CreateNode(nodeId);
        node->m_IsLive = false;
    }

    node->m_Type = NodeType::Group;

    if (node->m_GroupBounds.GetSize() != size)
    {
        node->m_GroupBounds.Min = node->m_Bounds.Min;
        node->m_GroupBounds.Max = node->m_Bounds.Min + size;
        node->m_GroupBounds.Floor();
        MakeDirty(NodeEditor::SaveReasonFlags::Size, node);
    }
}

ImVec2 ed::EditorContext::GetNodePosition(NodeId nodeId)
{
    auto node = FindNode(nodeId);
    if (!node)
        return ImVec2(FLT_MAX, FLT_MAX);

    return node->m_Bounds.Min;
}

ImVec2 ed::EditorContext::GetNodeSize(NodeId nodeId)
{
    auto node = FindNode(nodeId);
    if (!node)
        return ImVec2(0, 0);

    return node->m_Bounds.GetSize();
}

void ed::EditorContext::SetNodeZPosition(NodeId nodeId, float z)
{
    auto node = FindNode(nodeId);
    if (!node)
    {
        node = CreateNode(nodeId);
        node->m_IsLive = false;
    }

    node->m_ZPosition = z;
}

float ed::EditorContext::GetNodeZPosition(NodeId nodeId)
{
    auto node = FindNode(nodeId);
    if (!node)
        return 0.0f;

    return node->m_ZPosition;
}

ed::Node* ed::EditorContext::FindNodeAt(const ImVec2& p)
{
    for (auto node : m_Nodes)
        if (node->TestHit(p))
            return node;

    return nullptr;
}

void ed::EditorContext::FindNodesInRect(const ImRect& r, vector<Node*>& result, bool append, bool includeIntersecting)
{
    if (!append)
        result.resize(0);

    if (ImRect_IsEmpty(r))
        return;

    for (auto node : m_Nodes)
        if (node->TestHit(r, includeIntersecting))
            result.push_back(node);
}

ed::Node* ed::EditorContext::FindNode(NodeId id)
{
    return FindItemInLinear(m_Nodes, id);
}

ed::Pin* ed::EditorContext::FindPin(PinId id)
{
    return FindItemIn(m_Pins, id);
}

ed::Link* ed::EditorContext::FindLink(LinkId id)
{
    return FindItemIn(m_Links, id);
}

ed::Object* ed::EditorContext::FindObject(ObjectId id)
{
    if (id.IsNodeId())
        return FindNode(id.AsNodeId());
    else if (id.IsLinkId())
        return FindLink(id.AsLinkId());
    else if (id.IsPinId())
        return FindPin(id.AsPinId());
    else
        return nullptr;
}

ed::Node* ed::EditorContext::GetNode(NodeId id)
{
    auto node = FindNode(id);
    if (!node)
        node = CreateNode(id);
    return node;
}

ed::Pin* ed::EditorContext::GetPin(PinId id, PinKind kind)
{
    if (auto pin = FindPin(id))
    {
        pin->m_Kind = kind;
        return pin;
    }
    else
        return CreatePin(id, kind);
}

ed::Link* ed::EditorContext::GetLink(LinkId id)
{
    if (auto link = FindLink(id))
        return link;
    else
        return CreateLink(id);
}

void ed::EditorContext::MakeDirty(SaveReasonFlags reason)
{
    m_Settings.MakeDirty(reason);
}

void ed::EditorContext::MakeDirty(SaveReasonFlags reason, Node* node)
{
    m_Settings.MakeDirty(reason, node);
}

ed::Link* ed::EditorContext::FindLinkAt(const ImVec2& p)
{
    for (auto& link : m_Links)
    {
        if (link->IsGuided())
        {
            int cpIndex = link->TestHitControlPoint(p, 10.0f / m_Canvas.View().Scale);
            if (cpIndex >= 0)
            {
                if (ImGui::IsMouseClicked(0) && !m_CurrentAction)
                {
                    m_EditingLink = link;
                    m_DraggingControlPointIndex = cpIndex;
                    m_ControlPointDragStart = link->m_GuidedData->ControlPoints[cpIndex].Position;
                }
                return link;
            }
        }
    }
    
    for (auto& link : m_Links)
        if (link->TestHit(p, c_LinkSelectThickness))
            return link;

    return nullptr;
}

int ed::EditorContext::GetNodeIds(NodeId* nodes, int size) const
{
    if (size <= 0)
        return 0;

    int result = 0;
    for (auto node : m_Nodes)
    {
        if (!node->m_IsLive)
            continue;

        *nodes++ = node->m_ID;
        ++result;
        if (--size == 0)
            break;
    }

    return result;
}

//------------------------------------------------------------------------------
// From imgui_node_editor_selection.cpp
//------------------------------------------------------------------------------

void ed::EditorContext::ClearSelection()
{
    for (auto& object : m_SelectedObjects)
        object->m_IsSelected = false;

    m_SelectedObjects.clear();
}

void ed::EditorContext::SelectObject(Object* object)
{
    m_SelectedObjects.push_back(object);
    object->m_IsSelected = true;
}

void ed::EditorContext::DeselectObject(Object* object)
{
    auto objectIt = std::find(m_SelectedObjects.begin(), m_SelectedObjects.end(), object);
    if (objectIt == m_SelectedObjects.end())
        return;

    object->m_IsSelected = false;
    m_SelectedObjects.erase(objectIt);
}

void ed::EditorContext::SetSelectedObject(Object* object)
{
    ClearSelection();
    SelectObject(object);
}

void ed::EditorContext::ToggleObjectSelection(Object* object)
{
    if (IsSelected(object))
        DeselectObject(object);
    else
        SelectObject(object);
}

bool ed::EditorContext::IsSelected(Object* object)
{
    return object && object->m_IsSelected;
}

const ed::vector<ed::Object*>& ed::EditorContext::GetSelectedObjects()
{
    return m_SelectedObjects;
}

bool ed::EditorContext::IsAnyNodeSelected()
{
    for (auto object : m_SelectedObjects)
        if (object->AsNode())
            return true;

    return false;
}

bool ed::EditorContext::IsAnyLinkSelected()
{
    for (auto object : m_SelectedObjects)
        if (object->AsLink())
            return true;

    return false;
}

bool ed::EditorContext::HasSelectionChanged()
{
    return m_LastSelectedObjects != m_SelectedObjects;
}

//------------------------------------------------------------------------------
// From imgui_node_editor_links.cpp
//------------------------------------------------------------------------------

void ed::EditorContext::FindLinksInRect(const ImRect& r, vector<Link*>& result, bool append)
{
    if (!append)
        result.resize(0);

    if (ImRect_IsEmpty(r))
        return;

    for (auto link : m_Links)
        if (link->TestHit(r))
            result.push_back(link);
}

bool ed::EditorContext::HasAnyLinks(NodeId nodeId) const
{
    for (auto link : m_Links)
    {
        if (!link->m_IsLive)
            continue;

        if (link->m_StartPin->m_Node->m_ID == nodeId || link->m_EndPin->m_Node->m_ID == nodeId)
            return true;
    }

    return false;
}

bool ed::EditorContext::HasAnyLinks(PinId pinId) const
{
    for (auto link : m_Links)
    {
        if (!link->m_IsLive)
            continue;

        if (link->m_StartPin->m_ID == pinId || link->m_EndPin->m_ID == pinId)
            return true;
    }

    return false;
}

int ed::EditorContext::BreakLinks(NodeId nodeId)
{
    int result = 0;
    for (auto link : m_Links)
    {
        if (!link->m_IsLive)
            continue;

        if (link->m_StartPin->m_Node->m_ID == nodeId || link->m_EndPin->m_Node->m_ID == nodeId)
        {
            if (GetItemDeleter().Add(link))
                ++result;
        }
    }
    return result;
}

int ed::EditorContext::BreakLinks(PinId pinId)
{
    int result = 0;
    for (auto link : m_Links)
    {
        if (!link->m_IsLive)
            continue;

        if (link->m_StartPin->m_ID == pinId || link->m_EndPin->m_ID == pinId)
        {
            if (GetItemDeleter().Add(link))
                ++result;
        }
    }
    return result;
}

void ed::EditorContext::FindLinksForNode(NodeId nodeId, vector<Link*>& result, bool add)
{
    if (!add)
        result.clear();

    for (auto link : m_Links)
    {
        if (!link->m_IsLive)
            continue;

        if (link->m_StartPin->m_Node->m_ID == nodeId || link->m_EndPin->m_Node->m_ID == nodeId)
            result.push_back(link);
    }
}

bool ed::EditorContext::PinHadAnyLinks(PinId pinId)
{
    auto pin = FindPin(pinId);
    if (!pin || !pin->m_IsLive)
        return false;

    return pin->m_HasConnection || pin->m_HadConnection;
}

void ed::EditorContext::NotifyLinkDeleted(Link* link)
{
    if (m_LastActiveLink == link)
        m_LastActiveLink = nullptr;
}

ed::Link* ed::EditorContext::CreateLink(LinkId id)
{
    IM_ASSERT(nullptr == FindObject(id));
    auto link = new Link(this, id);
    m_Links.push_back({id, link});
    std::sort(m_Links.begin(), m_Links.end());

    return link;
}

ed::GuidedLink* ed::EditorContext::GetGuidedLinkData(LinkId id)
{
    auto it = m_GuidedLinks.find(id);
    return (it != m_GuidedLinks.end()) ? it->second : nullptr;
}

ed::GuidedLink* ed::EditorContext::CreateGuidedLinkData(LinkId id)
{
    auto existing = GetGuidedLinkData(id);
    if (existing)
    {
        return existing;
    }
    
    auto guidedLink = new ed::GuidedLink(id);
    m_GuidedLinks[id] = guidedLink;
    
    if (!m_Settings.FindLink(id))
        m_Settings.AddLink(id);
    
    return guidedLink;
}

void ed::EditorContext::DestroyGuidedLinkData(LinkId id)
{
    auto it = m_GuidedLinks.find(id);
    if (it != m_GuidedLinks.end())
    {
        delete it->second;
        m_GuidedLinks.erase(it);
    }
    
    m_Settings.RemoveLink(id);
}

bool ed::EditorContext::IsEditingLink(const Link* link) const
{
    return link && m_EditingLink == link;
}

void ed::EditorContext::HandleControlPointDragging()
{
    if (!m_EditingLink || m_DraggingControlPointIndex < 0)
        return;

    if (ImGui::IsMouseDragging(0, 0.0f))
    {
        auto dragOffset = ImGui::GetMouseDragDelta(0, 0.0f);
        
        auto newPos = m_ControlPointDragStart + dragOffset;
        
        if (m_DraggingControlPointIndex < static_cast<int>(m_EditingLink->m_GuidedData->ControlPoints.size()))
        {
            m_EditingLink->m_GuidedData->ControlPoints[m_DraggingControlPointIndex].Position = 
                m_EditingLink->m_GuidedData->SnapToGrid(newPos);
            MakeDirty(SaveReasonFlags::User);
            
            // Mark link as user-manipulated (preserve waypoints, disable auto-adjustment)
            if (m_Config.MarkLinkUserManipulated)
            {
                m_Config.MarkLinkUserManipulated(m_EditingLink->m_ID, m_Config.UserPointer);
            }
        }
        
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    else if (!ImGui::IsMouseDown(0))
    {
        // Validate that the control point still exists before accessing it
        if (!m_EditingLink || !m_EditingLink->m_GuidedData || 
            m_DraggingControlPointIndex < 0 || 
            m_DraggingControlPointIndex >= static_cast<int>(m_EditingLink->m_GuidedData->ControlPoints.size()))
        {
            // Control point was removed or index is invalid - clear dragging state
            m_EditingLink = nullptr;
            m_DraggingControlPointIndex = -1;
            return;
        }
        
        auto& controlPoints = m_EditingLink->m_GuidedData->ControlPoints;
        auto& currentPoint = controlPoints[m_DraggingControlPointIndex];
        const float snapThreshold = 50.0f;
        
        std::vector<ImVec2> candidatePoints;
        
        m_EditingLink->UpdateEndpoints();
        candidatePoints.push_back(m_EditingLink->m_Start);
        candidatePoints.push_back(m_EditingLink->m_End);
        
        if (m_DraggingControlPointIndex > 0)
        {
            candidatePoints.push_back(controlPoints[m_DraggingControlPointIndex - 1].Position);
            
            if (m_DraggingControlPointIndex > 1)
                candidatePoints.push_back(controlPoints[m_DraggingControlPointIndex - 2].Position);
        }
        
        if (m_DraggingControlPointIndex < static_cast<int>(controlPoints.size()) - 1)
        {
            candidatePoints.push_back(controlPoints[m_DraggingControlPointIndex + 1].Position);
            
            if (m_DraggingControlPointIndex < static_cast<int>(controlPoints.size()) - 2)
                candidatePoints.push_back(controlPoints[m_DraggingControlPointIndex + 2].Position);
        }
        
        ImVec2 originalPos = currentPoint.Position;
        ImVec2 bestAlignment = originalPos;
        float bestXDist = FLT_MAX;
        float bestYDist = FLT_MAX;
        
        float bestCandidateX = originalPos.x;
        float bestCandidateY = originalPos.y;
        
        for (const auto& candidate : candidatePoints)
        {
            float xDist = fabs(originalPos.x - candidate.x);
            float yDist = fabs(originalPos.y - candidate.y);
            
            if (xDist < snapThreshold && xDist < bestXDist)
            {
                bestXDist = xDist;
                bestCandidateX = candidate.x;
            }
            
            if (yDist < snapThreshold && yDist < bestYDist)
            {
                bestYDist = yDist;
                bestCandidateY = candidate.y;
            }
        }
        
        if (fabs(bestCandidateX - originalPos.x) > 0.01f)
            bestAlignment.x = bestCandidateX;
        
        if (fabs(bestCandidateY - originalPos.y) > 0.01f)
            bestAlignment.y = bestCandidateY;
        
        currentPoint.Position = bestAlignment;
        
        if (bestAlignment.x != originalPos.x || bestAlignment.y != originalPos.y)
        {
            MakeDirty(SaveReasonFlags::User);
            
            // Mark link as user-manipulated (preserve waypoints, disable auto-adjustment)
            if (m_Config.MarkLinkUserManipulated)
            {
                m_Config.MarkLinkUserManipulated(m_EditingLink->m_ID, m_Config.UserPointer);
            }
        }
        
        m_EditingLink = nullptr;
        m_DraggingControlPointIndex = -1;
    }
}

void ed::EditorContext::HandleGuidedLinkInteractions(const Control& control)
{
    if (m_CurrentAction)
        return;

    if (!control.DoubleClickedLink)
        return;

    auto link = control.DoubleClickedLink;
    
    auto mouseCanvasPos = ImGui::GetMousePos();
    
    link->UpdateEndpoints();
    
    if (link->IsGuided())
    {
        int cpIndex = link->TestHitControlPoint(mouseCanvasPos, 10.0f / m_Canvas.View().Scale);
        if (cpIndex >= 0)
        {
            link->m_GuidedData->RemoveControlPoint(cpIndex);
            MakeDirty(SaveReasonFlags::User);
            
            // Clear dragging state if the removed control point was being dragged
            if (m_EditingLink == link && m_DraggingControlPointIndex == cpIndex)
            {
                m_EditingLink = nullptr;
                m_DraggingControlPointIndex = -1;
            }
            // Adjust dragging index if a control point before the dragged one was removed
            else if (m_EditingLink == link && m_DraggingControlPointIndex >= 0 && m_DraggingControlPointIndex > cpIndex)
            {
                m_DraggingControlPointIndex--;
            }
            
            // Mark link as user-manipulated (preserve waypoints, disable auto-adjustment)
            if (m_Config.MarkLinkUserManipulated)
            {
                m_Config.MarkLinkUserManipulated(link->m_ID, m_Config.UserPointer);
            }
            
            if (link->m_GuidedData->ControlPoints.empty())
                link->m_GuidedData->Mode = LinkMode::Auto;
        }
        else
        {
            AddControlPointToGuidedLink(link, mouseCanvasPos);
            
            // Mark link as user-manipulated (preserve waypoints, disable auto-adjustment)
            if (m_Config.MarkLinkUserManipulated)
            {
                m_Config.MarkLinkUserManipulated(link->m_ID, m_Config.UserPointer);
            }
        }
    }
    else
    {
        ConvertLinkToGuidedMode(link, mouseCanvasPos);
        
        // Mark link as user-manipulated (preserve waypoints, disable auto-adjustment)
        if (m_Config.MarkLinkUserManipulated)
        {
            m_Config.MarkLinkUserManipulated(link->m_ID, m_Config.UserPointer);
        }
    }
}

void ed::EditorContext::AddControlPointToGuidedLink(Link* link, const ImVec2& mousePos)
{
    ImVec2 pointOnPath = mousePos;
    
    std::vector<ImVec2> points;
    points.push_back(link->m_Start);
    for (const auto& cp : link->m_GuidedData->ControlPoints)
        points.push_back(cp.Position);
    points.push_back(link->m_End);
    
    float minDistSq = FLT_MAX;
    for (size_t i = 0; i < points.size() - 1; ++i)
    {
        const ImVec2& p0 = points[i];
        const ImVec2& p1 = points[i + 1];
        const ImVec2 delta = p1 - p0;
        const float lengthSq = delta.x * delta.x + delta.y * delta.y;
        
        if (lengthSq > 0.0001f)
        {
            const ImVec2 pointDelta = mousePos - p0;
            float t = (pointDelta.x * delta.x + pointDelta.y * delta.y) / lengthSq;
            t = ImClamp(t, 0.0f, 1.0f);
            
            const ImVec2 projected = p0 + delta * t;
            const ImVec2 diff = mousePos - projected;
            const float distSq = diff.x * diff.x + diff.y * diff.y;
            
            if (distSq < minDistSq)
            {
                minDistSq = distSq;
                pointOnPath = projected;
            }
        }
    }
    
    link->m_GuidedData->AddControlPoint(pointOnPath);
    MakeDirty(SaveReasonFlags::User);
}

void ed::EditorContext::ConvertLinkToGuidedMode(Link* link, const ImVec2& mousePos)
{
    ImVec2 pointOnPath = mousePos;
    
    const auto bezier = link->GetCurve();
    const auto projection = ImProjectOnCubicBezier(mousePos, bezier.P0, bezier.P1, bezier.P2, bezier.P3, 50);
    pointOnPath = projection.Point;
    
    auto guidedData = CreateGuidedLinkData(link->m_ID);
    if (guidedData)
    {
        guidedData->Mode = LinkMode::Guided;
        guidedData->AddControlPoint(pointOnPath);
        link->m_GuidedData = guidedData;
        MakeDirty(SaveReasonFlags::User);
    }
}

void ed::EditorContext::ShowControlPointHoverCursor(const Control& control)
{
    if (!control.HotLink || !control.HotLink->IsGuided())
        return;
    
    if (control.HotNode || control.HotPin || control.ActiveNode)
        return;

    auto link = control.HotLink;
    auto mousePos = ToCanvas(ImGui::GetMousePos());
    
    int cpIndex = link->TestHitControlPoint(mousePos, 10.0f / m_Canvas.View().Scale);
    
    if (cpIndex >= 0)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
}

//------------------------------------------------------------------------------
// From imgui_node_editor_animation.cpp
//------------------------------------------------------------------------------

void ed::EditorContext::DrawAnimations()
{
    for (auto controller : m_AnimationControllers)
        controller->Draw(m_DrawList);
}

void ed::EditorContext::RegisterAnimation(Animation* animation)
{
    m_LiveAnimations.push_back(animation);
}

void ed::EditorContext::UnregisterAnimation(Animation* animation)
{
    auto it = std::find(m_LiveAnimations.begin(), m_LiveAnimations.end(), animation);
    if (it != m_LiveAnimations.end())
        m_LiveAnimations.erase(it);
}

void ed::EditorContext::UpdateAnimations()
{
    m_LastLiveAnimations = m_LiveAnimations;

    for (auto animation : m_LastLiveAnimations)
    {
        const bool isLive = (std::find(m_LiveAnimations.begin(), m_LiveAnimations.end(), animation) != m_LiveAnimations.end());

        if (isLive)
            animation->Update();
    }
}

void ed::EditorContext::Flow(Link* link, FlowDirection direction)
{
    m_FlowAnimationController.Flow(link, direction);
}

//------------------------------------------------------------------------------
// From imgui_node_editor_runtime.cpp
//------------------------------------------------------------------------------

void ed::EditorContext::ExecuteRuntime()
{
    if (!m_Config.UserPointer)
        return;
    
    typedef void (*ExecuteRuntimeCallback)(void* app);
    static ExecuteRuntimeCallback s_RuntimeCallback = nullptr;
    static void* s_RuntimeApp = nullptr;
    
    if (s_RuntimeCallback && s_RuntimeApp == m_Config.UserPointer)
    {
        s_RuntimeCallback(s_RuntimeApp);
    }
}

void ed::EditorContext::RegisterRuntimeCallback(void* app, void (*callback)(void*))
{
    typedef void (*ExecuteRuntimeCallback)(void* app);
    static ExecuteRuntimeCallback s_RuntimeCallback = nullptr;
    static void* s_RuntimeApp = nullptr;
    
    s_RuntimeApp = app;
    s_RuntimeCallback = callback;
}

//------------------------------------------------------------------------------
// From imgui_node_editor_store.cpp
//------------------------------------------------------------------------------

void ed::EditorContext::LoadSettings()
{
    auto settingsData = m_Config.Load();
    ed::Settings::Parse(settingsData, m_Settings);
    
    if (ImRect_IsEmpty(m_Settings.m_VisibleRect))
    {
        m_NavigateAction.m_Scroll = m_Settings.m_ViewScroll;
        m_NavigateAction.m_Zoom   = m_Settings.m_ViewZoom;    
    }
}

void ed::EditorContext::SaveSettings()
{
    m_Config.BeginSave();
    
    int savedNodeCount = 0;
    int liveNodeCount = 0;

    for (auto& node : m_Nodes)
    {
        if (node->m_IsLive)
            liveNodeCount++;
        
        auto settings = m_Settings.FindNode(node->m_ID);
        settings->m_Location = node->m_Bounds.Min;
        settings->m_Size     = node->m_Bounds.GetSize();
        if (IsGroup(node))
            settings->m_GroupSize = node->m_GroupBounds.GetSize();

        if (!node->m_RestoreState && settings->m_IsDirty && m_Config.SaveNodeSettings)
        {
            if (m_Config.SaveNode(node->m_ID, settings->Serialize().dump(), settings->m_DirtyReason))
                settings->ClearDirty();
        }
        
        if (settings->m_WasUsed)
            savedNodeCount++;
    }
    
    
    for (auto& linkPair : m_GuidedLinks)
    {
        auto linkId = linkPair.first;
        auto guidedData = linkPair.second;
        
        if (guidedData->Mode != LinkMode::Auto)
        {
            auto settings = m_Settings.FindLink(linkId);
            if (!settings)
                settings = m_Settings.AddLink(linkId);
            
            settings->m_Mode = guidedData->Mode;
            settings->m_EnableSnapping = guidedData->EnableSnapping;
            settings->m_SnapGridSize = guidedData->SnapGridSize;
            settings->m_ControlPoints.clear();
            
            if (guidedData->Mode == LinkMode::Guided)
            {
                for (const auto& cp : guidedData->ControlPoints)
                    settings->m_ControlPoints.push_back(cp.Position);
            }           
            
        }
    }

    m_Settings.m_Selection.resize(0);
    for (auto& object : m_SelectedObjects)
        m_Settings.m_Selection.push_back(object->ID());

    m_Settings.m_ViewScroll  = m_NavigateAction.m_Scroll;
    m_Settings.m_ViewZoom    = m_NavigateAction.m_Zoom;
    m_Settings.m_VisibleRect = m_NavigateAction.m_VisibleRect;

    if (m_Config.Save(m_Settings.Serialize(), m_Settings.m_DirtyReason))
        m_Settings.ClearDirty();

    m_Config.EndSave();
}

