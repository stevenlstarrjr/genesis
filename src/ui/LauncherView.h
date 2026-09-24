#pragma once
#include "ui/Toolkit.h"

namespace genesis::ui {
// Reusable view only. The platform host owns dialogs, files and window events.
class LauncherView {
public:
    LauncherView(std::string message, std::function<void()> openFolder, std::function<void()> openFile);
    Document& document() { return m_document; }
    void setMessage(std::string message);
    void setBusy(bool busy);
    void layout(float width, float height);
    const Surface& render(float width, float height, float scale = 1);
private:
    Document m_document;
    Node *m_card, *m_actions, *m_title, *m_status, *m_folder, *m_file;
    float m_width = 0;
};
}
