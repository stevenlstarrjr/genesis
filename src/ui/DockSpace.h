#pragma once
#include "ui/Toolkit.h"
#include <array>
#include <map>
#include <optional>
#include <set>

namespace genesis::ui {
// A binary split tree. Leaves are ordered tab groups; absent panel IDs are closed.
// Hosts own persistence and can store this small, renderer-independent value.
struct DockLayout {
    Direction axis=Direction::Row;
    float ratio=.5f;
    std::vector<DockLayout> children;
    std::vector<std::string> tabs;
    std::string active;
    static DockLayout group(std::vector<std::string> tabs);
    static DockLayout split(Direction axis,float ratio,DockLayout first,DockLayout second);
};
enum class DockEdge { Center, Left, Right, Top, Bottom };

class DockSpace {
public:
    DockSpace(Document& document,Node& parent,std::function<void()> changed={});
    Node& add(std::string id,std::string title,Icon icon,float minWidth=160,float minHeight=120,
        std::string blenderIcon={});
    bool setLayout(DockLayout layout); // Rejects duplicates, unknown IDs and invalid split trees atomically.
    const DockLayout& layout() const { return m_layout; }
    void arrange(); // Call after document layout; respects pane minimums as the window shrinks.
    bool dock(const std::string& panel,const std::string& target,DockEdge edge,size_t tabIndex=SIZE_MAX);
    bool selectEditor(const std::string& current,const std::string& next); // Change an area's editor type.
    void openEditorMenu(const std::string& panel,Node& anchor);
    Rect editorButtonBounds(const std::string& panel) const;
    void show(const std::string& panel);
    void close(const std::string& panel);
    bool visible(const std::string& panel) const; // Open and active in its group.
    bool open(const std::string& panel) const;
    Rect panelBounds(const std::string& panel) const;
    Rect tabBounds(const std::string& panel) const;
    // Lets an editor be open in several areas at once, as in Blender. Copies
    // are "<id>#<n>" panels built by `build`. Splitting an area of this type
    // opens a copy, and so does choosing it for an area when it is open elsewhere.
    void setDuplicable(const std::string& id,std::function<void(Node& page,const std::string& instance)> build);
    static std::string editorType(const std::string& panel); // "console#2" -> "console".
    bool dragging() const { return !m_dragPanel.empty() || m_cornerLayout; }
private:
    struct Panel { Node* content; std::string title; Icon icon; float minWidth,minHeight; std::string blenderIcon; };
    struct Group {
        DockLayout* layout; Node *node,*header;
        Node* editorButton{};
        std::map<std::string,Node*> tabs;
        std::array<Node*,4> handles{};
    };
    struct Split { DockLayout* layout; Node *node,*first,*second; };
    Document& m_document;
    Node *m_root,*m_tree,*m_parking,*m_preview,*m_overlay,*m_editorPopup;
    DockLayout m_layout;
    std::map<std::string,Panel> m_panels;
    std::vector<std::string> m_editorOrder;
    std::vector<Node*> m_editorChoices;
    std::vector<Group> m_groups;
    std::vector<Split> m_splits;
    std::function<void()> m_changed;
    std::string m_dragPanel,m_target;
    DockEdge m_edge=DockEdge::Center;
    size_t m_tabIndex=SIZE_MAX;
    // Blender-style corner gesture, resolved live under the cursor: inside
    // the source area it splits; over an adjacent area it joins (removes) it.
    enum class CornerAction { None, Split, Join, Swap };
    // An area's rectangle including half of each adjoining splitter.
    struct Cell { Rect r; DockLayout leaf; };
    std::string m_cornerTarget;
    DockLayout *m_cornerPending=nullptr,*m_cornerLayout=nullptr;
    Node* m_cornerHandle=nullptr;
    size_t m_corner=0;
    float m_cornerPressX=0,m_cornerPressY=0;
    std::optional<Direction> m_cornerAxis;
    CornerAction m_cornerAction=CornerAction::None;
    bool m_cornerBefore=false; // Split: the new area comes first.
    bool m_cornerSwap=false; // Ctrl held at press: swap contents with the area under the cursor.
    float m_cornerRatio=.5f;
    uint32_t m_nextSplitArea=1;
    std::map<std::string,std::function<void(Node&,const std::string&)>> m_builders;
    std::map<std::string,uint32_t> m_nextInstance;
    bool instanceId(const std::string& id) const;
    void addInstance(const std::string& id);
    // A new or parked copy of `type` that is not in `taken`; the id is added to it.
    std::string newInstance(const std::string& type,std::set<std::string>& taken);
    std::set<std::string> openIds(const DockLayout& layout) const;
    // Empty leaves become copies of the editor type named in `active`, else placeholders.
    void fillEmpty(DockLayout& layout);
    void rebuild();
    void build(Node& parent,DockLayout& layout);
    void activate(const std::string& panel,bool focus=false);
    void drag(const std::string& panel,float x,float y);
    void finishDrag(bool commit);
    // Corner-gesture feedback drawn over the workspace; never takes input.
    bool m_overlayShown=false;
    void clearOverlay();
    void overlayRect(Rect bounds,Color fill,float radius=12);
    void overlayArrow(Rect bounds,Direction axis,bool forward);
    std::vector<Cell> cells() const;
    std::optional<DockLayout> tile(const std::vector<Cell>& cells) const; // Empty leaves stay unnamed.
    std::optional<std::vector<Cell>> planJoin(const std::string& source,const std::string& target,Rect& removed) const;
    void dragCorner(float x,float y);
    void finishCornerDrag(bool commit);
    void addSplitArea(const std::string& id);
    std::pair<float,float> minimum(const DockLayout& layout) const;
    void size(DockLayout& layout,float width,float height);
    DockLayout* find(const std::string& panel);
    void notify();
};
}
