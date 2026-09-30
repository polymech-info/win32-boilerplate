#pragma once
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

// Forward declaration
class App;

// Callback to mark link as user-manipulated
using EdgeEditorMarkLinkCallback = void (*)(ed::LinkId linkId, void* userData);

// Edge editing system for fine-tuning guided link segments
struct EdgeEditor
{
    ed::LinkId EditingLinkId;      // Link being edited
    int EditingEdgeIndex;          // Which edge (0 = first segment, etc.)
    ImVec2 DragStartPos;           // Where drag started
    bool IsDragging;               // Currently dragging an edge
    EdgeEditorMarkLinkCallback MarkLinkCallback;  // Callback to mark link as user-manipulated
    void* MarkLinkUserData;        // User data for callback
    
    EdgeEditor()
        : EditingLinkId(0)
        , EditingEdgeIndex(-1)
        , DragStartPos(0, 0)
        , IsDragging(false)
        , MarkLinkCallback(nullptr)
        , MarkLinkUserData(nullptr)
    {
    }
    
    // Process edge selection and dragging
    // Returns true if handled (blocks other interactions)
    bool Process();
    
    // Draw visual feedback for hovered/selected edges
    void DrawFeedback();
};

