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
// Node Settings
//
//------------------------------------------------------------------------------
void ed::NodeSettings::ClearDirty()
{
    m_IsDirty     = false;
    m_DirtyReason = SaveReasonFlags::None;
}

void ed::NodeSettings::MakeDirty(SaveReasonFlags reason)
{
    m_IsDirty     = true;
    m_DirtyReason = m_DirtyReason | reason;
}

ed::json::value ed::NodeSettings::Serialize()
{
    json::value result;
    result["location"]["x"] = m_Location.x;
    result["location"]["y"] = m_Location.y;

    if (m_GroupSize.x > 0 || m_GroupSize.y > 0)
    {
        result["group_size"]["x"] = m_GroupSize.x;
        result["group_size"]["y"] = m_GroupSize.y;
    }

    return result;
}

bool ed::NodeSettings::Parse(const std::string& string, NodeSettings& settings)
{
    auto settingsValue = json::value::parse(string);
    if (settingsValue.is_discarded())
        return false;

    return Parse(settingsValue, settings);
}

bool ed::NodeSettings::Parse(const json::value& data, NodeSettings& result)
{
    if (!data.is_object())
        return false;

    auto tryParseVector = [](const json::value& v, ImVec2& result) -> bool
    {
        if (v.is_object())
        {
            auto xValue = v["x"];
            auto yValue = v["y"];

            if (xValue.is_number() && yValue.is_number())
            {
                result.x = static_cast<float>(xValue.get<double>());
                result.y = static_cast<float>(yValue.get<double>());

                return true;
            }
        }

        return false;
    };

    if (!tryParseVector(data["location"], result.m_Location))
        return false;

    if (data.contains("group_size") && !tryParseVector(data["group_size"], result.m_GroupSize))
        return false;

    return true;
}

//------------------------------------------------------------------------------
//
// Settings
//
//------------------------------------------------------------------------------
ed::NodeSettings* ed::Settings::AddNode(NodeId id)
{
    m_Nodes.push_back(NodeSettings(id));
    return &m_Nodes.back();
}

ed::NodeSettings* ed::Settings::FindNode(NodeId id)
{
    for (auto& settings : m_Nodes)
        if (settings.m_ID == id)
            return &settings;

    return nullptr;
}

void ed::Settings::RemoveNode(NodeId id)
{
    auto node = FindNode(id);
    if (!node)
        return;

    *node = NodeSettings(id);
}

ed::LinkSettings* ed::Settings::AddLink(LinkId id)
{
    m_Links.push_back(LinkSettings(id));
    return &m_Links.back();
}

ed::LinkSettings* ed::Settings::FindLink(LinkId id)
{
    for (auto& settings : m_Links)
        if (settings.m_ID == id)
            return &settings;

    return nullptr;
}

void ed::Settings::RemoveLink(LinkId id)
{
    auto link = FindLink(id);
    if (!link)
        return;

    *link = LinkSettings(id);
}

void ed::Settings::ClearDirty(Node* node)
{
    if (node)
    {
        auto settings = FindNode(node->m_ID);
        IM_ASSERT(settings);
        settings->ClearDirty();
    }
    else
    {
        m_IsDirty     = false;
        m_DirtyReason = SaveReasonFlags::None;

        for (auto& knownNode : m_Nodes)
            knownNode.ClearDirty();
    }
}

void ed::Settings::MakeDirty(SaveReasonFlags reason, Node* node)
{
    m_IsDirty     = true;
    m_DirtyReason = m_DirtyReason | reason;

    if (node)
    {
        auto settings = FindNode(node->m_ID);
        IM_ASSERT(settings);

        settings->MakeDirty(reason);
    }
}

std::string ed::Settings::Serialize()
{
    json::value result;

    auto serializeObjectId = [](ObjectId id)
    {
        auto value = std::to_string(reinterpret_cast<uintptr_t>(id.AsPointer()));
        switch (id.Type())
        {
            default:
            case NodeEditor::Detail::ObjectType::None: return value;
            case NodeEditor::Detail::ObjectType::Node: return "node:" + value;
            case NodeEditor::Detail::ObjectType::Link: return "link:" + value;
            case NodeEditor::Detail::ObjectType::Pin:  return "pin:"  + value;
        }
    };

    auto& nodes = result["nodes"];
    for (auto& node : m_Nodes)
    {
        if (node.m_WasUsed)
            nodes[serializeObjectId(node.m_ID)] = node.Serialize();
    }
    
    auto& links = result["links"];
    for (auto& link : m_Links)
    {
        // Save all non-Auto modes (Straight, Guided)
        if (link.m_Mode != LinkMode::Auto)
            links[serializeObjectId(link.m_ID)] = link.Serialize();
    }

    auto& selection = result["selection"];
    for (auto& id : m_Selection)
        selection.push_back(serializeObjectId(id));

    auto& view = result["view"];
    view["scroll"]["x"] = m_ViewScroll.x;
    view["scroll"]["y"] = m_ViewScroll.y;
    view["zoom"]   = m_ViewZoom;
    view["visible_rect"]["min"]["x"] = m_VisibleRect.Min.x;
    view["visible_rect"]["min"]["y"] = m_VisibleRect.Min.y;
    view["visible_rect"]["max"]["x"] = m_VisibleRect.Max.x;
    view["visible_rect"]["max"]["y"] = m_VisibleRect.Max.y;

    return result.dump(4);  // Pretty print with 4 space indent
}

bool ed::Settings::Parse(const std::string& string, Settings& settings)
{
    Settings result = settings;

    auto settingsValue = json::value::parse(string);
    if (settingsValue.is_discarded())
        return false;

    if (!settingsValue.is_object())
        return false;

    auto tryParseVector = [](const json::value& v, ImVec2& result) -> bool
    {
        if (v.is_object() && v.contains("x") && v.contains("y"))
        {
            auto xValue = v["x"];
            auto yValue = v["y"];

            if (xValue.is_number() && yValue.is_number())
            {
                result.x = static_cast<float>(xValue.get<double>());
                result.y = static_cast<float>(yValue.get<double>());

                return true;
            }
        }

        return false;
    };

    auto deserializeObjectId = [](const std::string& str)
    {
        auto separator = str.find_first_of(':');
        auto idStart   = str.c_str() + ((separator != std::string::npos) ? separator + 1 : 0);
        auto id        = reinterpret_cast<void*>(strtoull(idStart, nullptr, 10));
        if (str.compare(0, separator, "node") == 0)
            return ObjectId(NodeId(id));
        else if (str.compare(0, separator, "link") == 0)
            return ObjectId(LinkId(id));
        else if (str.compare(0, separator, "pin") == 0)
            return ObjectId(PinId(id));
        else
            // fallback to old format
            return ObjectId(NodeId(id)); //return ObjectId();
    };

    //auto& settingsObject = settingsValue.get<json::object>();

    auto& nodesValue = settingsValue["nodes"];
    if (nodesValue.is_object())
    {
        for (auto& node : nodesValue.get<json::object>())
        {
            auto id = deserializeObjectId(node.first.c_str()).AsNodeId();

            auto nodeSettings = result.FindNode(id);
            if (!nodeSettings)
                nodeSettings = result.AddNode(id);

            NodeSettings::Parse(node.second, *nodeSettings);
        }
    }
    
    auto& linksValue = settingsValue["links"];
    if (linksValue.is_object())
    {
        for (auto& link : linksValue.get<json::object>())
        {
            auto id = deserializeObjectId(link.first.c_str()).AsLinkId();

            auto linkSettings = result.FindLink(id);
            if (!linkSettings)
                linkSettings = result.AddLink(id);

            LinkSettings::Parse(link.second, *linkSettings);
        }
    }

    auto& selectionValue = settingsValue["selection"];
    if (selectionValue.is_array())
    {
        const auto selectionArray = selectionValue.get<json::array>();

        result.m_Selection.reserve(selectionArray.size());
        result.m_Selection.resize(0);
        for (auto& selection : selectionArray)
        {
            if (selection.is_string())
                result.m_Selection.push_back(deserializeObjectId(selection.get<json::string>()));
        }
    }

    auto& viewValue = settingsValue["view"];
    if (viewValue.is_object())
    {
        auto& viewScrollValue = viewValue["scroll"];
        auto& viewZoomValue   = viewValue["zoom"];

        if (!tryParseVector(viewScrollValue, result.m_ViewScroll))
            result.m_ViewScroll = ImVec2(0, 0);

        result.m_ViewZoom = viewZoomValue.is_number() ? static_cast<float>(viewZoomValue.get<double>()) : 1.0f;

        if (!viewValue.contains("visible_rect") || !tryParseVector(viewValue["visible_rect"]["min"], result.m_VisibleRect.Min) || !tryParseVector(viewValue["visible_rect"]["max"], result.m_VisibleRect.Max))
            result.m_VisibleRect = {};
    }

    settings = std::move(result);

    return true;
}


