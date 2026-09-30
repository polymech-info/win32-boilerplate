https://github.com/openinterpreter/open-interpreter.git
https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/UIAutomation?utm_source=chatgpt.com

#include <UIAutomation.h>
#pragma comment(lib, "uiautomationcore.lib")



````md
# System-wide AI Input Layer for Win32 Applications

## Goal

Build a tray application that can interact with arbitrary desktop applications using:

- Global shortcuts
- STT (Speech-to-Text)
- TTS
- LLM processing
- UI Automation (UIA)
- Overlay UI
- Context extraction
- Smart write-back

Primary use cases:

```text
Ctrl+Alt+Space
    ↓
Capture focused application
    ↓
Show mic overlay
    ↓
Record speech
    ↓
Convert speech → prompt
    ↓
Capture local context
    ↓
Send to LLM
    ↓
Insert result back into application
```

Examples:

```text
"translate this"

"fix grammar"

"summarize"

"continue writing"

"replace selection"

"fill spreadsheet cells"

"reply to email"
```

---

# UI Automation Foundation

Windows already exposes accessibility APIs.

Inspect.exe is using:

```cpp
#include <UIAutomation.h>

#pragma comment(lib, "uiautomationcore.lib")
```

Runtime:

```text
your_app.exe
    ↓
UIAutomationCore.dll
    ↓
application accessibility provider
```

No DLL redistribution needed.

Windows provides:

```text
C:\Windows\System32\UIAutomationCore.dll
```

Commercial use is fine.

---

# Focus Acquisition

On shortcut:

```cpp
IUIAutomation* uia;

uia->GetFocusedElement(&element);
```

Read metadata:

```cpp
element->get_CurrentName(...)
element->get_CurrentClassName(...)
element->get_CurrentControlType(...)
element->get_CurrentBoundingRectangle(...)
element->get_CurrentProcessId(...)
```

Produces:

```cpp
FocusedTarget
{
    process;
    title;
    controlType;
    bounds;
}
```

---

# Tree Traversal

Move upward:

```cpp
IUIAutomationTreeWalker* walker;

walker->GetParentElement(...)
```

Typical hierarchy:

```text
focused element
    ↓
parent
    ↓
document
    ↓
window
```

Look for:

```text
UIA_EditControlTypeId
UIA_DocumentControlTypeId
UIA_TextControlTypeId
UIA_PaneControlTypeId
```

---

# Overlay HUD

Mic overlay:

```text
focused element
    ↓
bounding rectangle
    ↓
render overlay near field
```

Window style:

```cpp
WS_POPUP

WS_EX_TOPMOST
WS_EX_LAYERED
WS_EX_NOACTIVATE
WS_EX_TOOLWINDOW
```

Optional:

```cpp
WS_EX_TRANSPARENT
```

Position:

```cpp
x = rect.right - 28
y = rect.top + 4
```

---

# Reading Content

Do not scrape full applications.

Read hierarchy:

```text
1. Selection
2. Focused control
3. Parent context
4. Visible viewport
5. Full document
```

Read chain:

```cpp
read_selected_text()

1. UIA TextPattern selection
2. Ctrl+C clipboard capture
3. ValuePattern value
4. Parent TextPattern
```

---

# Context Levels

```cpp
enum class ContextLevel
{
    None,
    SelectionOnly,
    FocusedElement,
    SurroundingBlock,
    VisibleViewport,
    FullDocument
};
```

Examples:

```text
"fix this"

SelectionOnly
```

```text
"summarize page"

VisibleViewport
```

```text
"fill spreadsheet"

SelectedRange
```

---

# UI Automation Patterns

Simple edit fields:

```cpp
IUIAutomationValuePattern
```

Read:

```cpp
get_CurrentValue(...)
```

Write:

```cpp
SetValue(...)
```

Rich text:

```cpp
IUIAutomationTextPattern
```

Read:

```cpp
GetSelection()
GetDocumentRange()
GetText()
```

TextPattern often:

```text
Read: yes

Write: usually no
```

---

# Write Priority

Preferred order:

```text
1. Application SDK
2. UIA SetValue
3. Clipboard paste
4. SendInput
5. App-specific hacks
```

Clipboard is often most reliable.

Flow:

```text
save clipboard
copy output
Ctrl+V
restore clipboard
```

Works for:

```text
Notepad
LibreOffice
Word
Browser fields
Electron
WebView2
Many editors
```

---

# Core Architecture

```text
Tray App
    ├── GlobalHotkeyManager
    ├── UiaFocusTracker
    ├── OverlayHud
    ├── ClipboardBroker
    ├── SttEngine
    ├── TtsEngine
    ├── LlmRouter
    ├── ContextExtractor
    ├── TargetResolver
    ├── WriteBackAdapter
    └── AppAdapterRegistry
```

---

# Runtime Flow

```text
Ctrl+Alt+Space
        ↓
GetFocusedElement()
        ↓
Get bounds
        ↓
Show mic HUD
        ↓
Record speech
        ↓
Convert speech → prompt
        ↓
Capture context
        ↓
LLM
        ↓
Write result
```

---

# Generic Context Model

```cpp
struct FocusedTarget
{
    std::wstring app;

    std::wstring windowTitle;

    std::wstring focusedText;

    std::wstring selectedText;

    std::wstring visibleText;

    std::wstring surroundingText;

    RECT bounds;

    WriteCapabilities capabilities;
};
```

---

# Adapter Interface

```cpp
struct AppContext
{
    std::wstring app;

    std::wstring title;

    std::wstring selectedText;

    std::wstring visibleText;

    std::wstring localContext;

    RECT targetRect;
};


struct AppAdapter
{
    bool canHandle(
        ProcessInfo process,
        UIElement element);

    AppContext capture(
        ContextRequest request);

    WriteResult write(
        WriteRequest request);
};
```

---

# Generic Adapter Strategy

Most applications should use:

```text
GenericUIAAdapter
        ↓
Clipboard fallback
```

Only create custom adapters for valuable targets.

Priority:

```text
1. Generic UIA adapter
2. Clipboard adapter
3. Notepad adapter
4. LibreOffice adapter
5. Browser adapter
6. Word adapter
7. VSCode adapter
```

---

# App Fingerprinting

Instead of maintaining thousands of rules:

```cpp
FocusedElement
{
    processName;
    className;
    automationId;
    controlType;
    frameworkId;
    parentChain;
}
```

Examples:

```text
Chrome

process:
    chrome.exe

framework:
    Chrome

control:
    Edit
```

```text
LibreOffice

process:
    soffice.bin

class:
    SALFRAME

control:
    Document
```

```text
Notepad

process:
    notepad.exe

class:
    RichEditD2DPT
```

---

# Adapter Registry

```json
{
    "chrome.exe":
    {
        "read":
        [
            "uia-text",
            "clipboard-copy"
        ],

        "write":
        [
            "clipboard-paste"
        ],

        "overlay":
        [
            "uia-rect"
        ]
    },

    "notepad.exe":
    {
        "read":
        [
            "uia-value",
            "uia-text"
        ],

        "write":
        [
            "uia-value",
            "clipboard-paste"
        ]
    },

    "soffice.bin":
    {
        "read":
        [
            "clipboard-copy"
        ],

        "write":
        [
            "clipboard-paste"
        ]
    }
}
```

---

# LibreOffice Spreadsheet Example

Instead of deep integration:

Read:

```text
Ctrl+C selected cells
```

Clipboard:

```text
TSV
HTML
```

LLM:

```text
Edit table
```

Write:

```text
Ctrl+V
```

---

# Example LLM Payload

```json
{
    "command":"make clearer",

    "target":
    {
        "app":"soffice.bin",

        "kind":"spreadsheet-cell",

        "title":"quote.xlsx"
    },

    "selection":
        "delivery 2 weeks maybe",

    "context":
    {
        "row":
        [
            "Item",
            "Qty",
            "Price",
            "Notes"
        ]
    },

    "writePolicy":
    {
        "preferred":
            "replace-selection",

        "allowed":
        [
            "paste",
            "uia-setvalue"
        ]
    }
}
```

---

# Write Policies

Never allow unrestricted writes.

Policies:

```text
replace-selection

insert-at-caret

append-after-selection

create-new-note

preview-first
```

Examples:

```text
"insert ..."
```

```text
insert-at-caret
```

```text
"replace ..."
```

```text
replace-selection
```

```text
"send email"
```

```text
preview-first
```

---

# Existing Open Source References

pywinauto:

https://pywinauto.readthedocs.io/

Features:

- UIA backend
- Win32 backend
- real-world heuristics

---

UIA-v2:

https://github.com/Descolada/UIA-v2

Features:

- browser examples
- Office examples
- Explorer examples

---

Accessibility Insights:

https://accessibilityinsights.io/

Features:

- UIA tree inspection
- pattern visualization

---

pywinauto-recorder:

https://github.com/beuaaa/pywinauto_recorder

Features:

- interaction recording
- selector generation

---

OpenAdapt:

https://github.com/OpenAdaptAI/OpenAdapt

Features:

- action recording
- workflow capture

---

Open Interpreter:

https://github.com/OpenInterpreter/open-interpreter

Features:

- desktop automation
- accessibility integration

---

# Product Evolution

V1:

```text
GetFocusedElement()

Mic overlay

STT

Capture selection

Clipboard fallback

Paste result
```

---

V2:

```text
Collect telemetry

How read succeeded

How write succeeded

Failure modes
```

---

V3:

```text
Automatic app fingerprints
```

---

V4:

```text
App-specific adapters
```

---

Final abstraction:

```text
TargetResolver
        ↓
ContextExtractor
        ↓
IntentRouter
        ↓
WriteBackAdapter
        ↓
OverlayUI
```

Result:

```text
System-wide AI Input Layer
```
````
