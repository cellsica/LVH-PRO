#pragma once
#include "UiCommon.h"

// =====================================================================
// SystemLogPanel — Application-level message log
//
// Displays timestamped system events (IPC connections, Bridge launches,
// errors, etc.) in a scrolling read-only text area.
// =====================================================================
class SystemLogPanel : public Component
{
public:
    SystemLogPanel();

    /** Push a message to the log (thread-safe via callAsync). */
    void pushMessage (const String& text);

    void paint   (Graphics& g) override;
    void resized ()             override;

private:
    TextEditor log;
    StringArray lines;
    static constexpr int maxLines = 500;

    void appendLine (const String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SystemLogPanel)
};
