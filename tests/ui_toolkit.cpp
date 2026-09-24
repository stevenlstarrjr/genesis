#include "ui/Toolkit.h"
#include "ui/LauncherView.h"
#include "ui/RendererHud.h"
#include "ui/EditorWidgets.h"
#include "ui/DockSpace.h"
#include "editor/UiWorkbench.h"
#include "editor/GameEditor.h"
#include "SceneDocument.h"
#include "GameplayWorld.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace genesis::ui;
namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
void near(float a, float b, const char* message) { require(std::abs(a-b) < 1.1f,message); }
// Small artifact writer; tests need neither a window nor a graphics backend.
void save(const Surface& surface, const std::filesystem::path& file) {
    if (file.empty()) return;
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file,std::ios::binary);
    const uint32_t bytes = surface.width*surface.height*4;
    auto u16 = [&](uint16_t value) { out.put(char(value)); out.put(char(value>>8)); };
    auto u32 = [&](uint32_t value) { u16(uint16_t(value)); u16(uint16_t(value>>16)); };
    out.put('B'); out.put('M'); u32(54+bytes); u32(0); u32(54); u32(40);
    u32(surface.width); u32(uint32_t(-int32_t(surface.height))); u16(1); u16(32);
    u32(0); u32(bytes); u32(0); u32(0); u32(0); u32(0);
    out.write(reinterpret_cast<const char*>(surface.pixels.data()),bytes);
    require(bool(out),"write UI image");
}
void layoutAndPaint() {
    Document doc;
    Style root; root.direction = Direction::Row; root.padding = 10; root.gap = 10;
    doc.root().setStyle(root);
    Style left; left.width = 100; left.shrink = 0; left.background = {200,40,20,255};
    auto& sidebar = doc.root().column().setStyle(left);
    Style rest; rest.grow = 1; rest.background = {10,160,40,128};
    auto& body = doc.root().column().setStyle(rest);
    const auto& surface = doc.render(400,200);
    near(sidebar.bounds().x,10,"padding x"); near(sidebar.bounds().width,100,"fixed sidebar");
    near(body.bounds().x,120,"row gap"); near(body.bounds().width,270,"flex fills rest");
    require(surface.pixels[20*400+20] == 0xffc82814,"ARGB packing");
    const auto transparent = surface.pixels[20*400+130];
    require((transparent>>24) == 128,"straight alpha opacity");
    require(((transparent>>8)&255) >= 158,"straight alpha is not premultiplied");
    require(surface.pixels[0] == 0,"transparent canvas clears");
    const auto revision = surface.revision;
    require(!doc.dirty() && doc.render(400,200).revision == revision,"clean document cached");
    const auto& scaled = doc.render(400,200,2);
    require(scaled.width == 800 && scaled.height == 400,"DPI physical extent");
    require(scaled.pixels[40*800+40] == 0xffc82814,"DPI drawing position");
    near(sidebar.bounds().width,100,"DPI retains logical layout");
    doc.layout(700,200); near(body.bounds().width,570,"responsive grow");
    sidebar.setVisible(false); doc.layout(400,200); near(body.bounds().width,380,"hidden removes layout and gap");
    bool rejected = false;
    try { doc.render(0,0); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected,"invalid viewport rejected");
    rejected = false; rest.fontSize = std::numeric_limits<float>::quiet_NaN();
    try { body.setStyle(rest); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected,"invalid styles rejected");
}
void input() {
    Document doc; int clicks = 0, otherClicks = 0;
    Style root; root.padding = 10; root.gap = 10; doc.root().setStyle(root);
    auto& a = doc.root().button("First",[&] { ++clicks; });
    auto& b = doc.root().button("Second",[&] { ++otherClicks; });
    doc.render(400,200);
    const float x = a.bounds().x+5, y = a.bounds().y+5;
    require(doc.pointerMove(x,y) && a.hovered(),"hover hit");
    require(doc.pointerDown(x,y) && a.pressed() && a.focused(),"press captures and focuses");
    doc.pointerMove(500,500); require(!a.pressed(),"drag out unpresses");
    doc.pointerUp(500,500); require(clicks == 0,"release outside cancels");
    doc.pointerDown(x,y); doc.pointerMove(500,500); doc.pointerMove(x,y); doc.pointerUp(x,y);
    require(clicks == 1,"drag back in activates once");
    doc.pointerUp(x,y); require(clicks == 1,"release without press ignored");
    doc.keyDown(Key::Tab); require(b.focused(),"tab forward");
    doc.keyDown(Key::Space); doc.keyDown(Key::Space,false,true);
    require(b.pressed() && otherClicks == 0,"space waits for release, ignores repeat");
    doc.keyUp(Key::Enter); require(otherClicks == 0,"wrong key release ignored");
    doc.keyUp(Key::Space); require(otherClicks == 1,"space activates once");
    doc.keyDown(Key::Tab,true); require(a.focused(),"shift tab backward");
    doc.keyDown(Key::Tab,true); require(b.focused(),"reverse focus wraps");
    b.setEnabled(false); doc.keyDown(Key::Tab); require(a.focused(),"disabled omitted from focus order");
    doc.pointerDown(x,y); a.setEnabled(false); doc.pointerUp(x,y); require(clicks == 1,"disable while pressed cancels");
    a.setEnabled(true); doc.keyDown(Key::Tab); doc.keyDown(Key::Enter); doc.cancelInput(); doc.keyUp(Key::Enter);
    require(clicks == 1 && !a.focused(),"focus loss cancels keyboard activation");
    doc.root().clear();
    auto& destructive = doc.root().button("Remove self",[&] { doc.root().clear(); ++clicks; });
    doc.layout(400,200); const auto box = destructive.bounds();
    doc.pointerDown(box.x+5,box.y+5); doc.pointerUp(box.x+5,box.y+5);
    require(clicks == 2,"callbacks may destroy their own widget safely");
    require(!doc.keyDown(Key::Tab),"empty tree has no focus targets");
    doc.render(400,200);
}
void textAndClipping() {
    Document doc;
    Style root; root.padding = 10; root.gap = 5; root.align = Align::Start;
    doc.root().setStyle(root);
    auto& thin = doc.root().label("iiii"); auto& wide = doc.root().label("WWWW");
    doc.layout(400,250);
    require(wide.bounds().width > thin.bounds().width*2,"text uses glyph metrics, not character count");
    const auto width = thin.bounds().width; thin.setText("a much longer text"); doc.layout(400,250);
    require(thin.bounds().width > width,"text mutation invalidates measure");
    Style wrap; wrap.width = 120; wrap.textWrap = TextWrap::Word;
    auto& paragraph = doc.root().label("A long line of words should wrap to several lines.").setStyle(wrap);
    doc.layout(400,400); require(paragraph.bounds().height > 40,"word wrapping measures multiline height");
    doc.root().clear();
    auto& wrapped = doc.root().label("Hello world");
    auto wordStyle = wrapped.style(); wordStyle.width = 64; wordStyle.textWrap = TextWrap::Word;
    wrapped.setStyle(wordStyle);
    auto wrapPixels = doc.render(180,100).pixels;
    wrapped.setText("Hello\nworld");
    auto explicitPixels = doc.render(180,100).pixels;
    require(wrapPixels == explicitPixels,"word wrapping matches explicit complete-word line breaks");
    wordStyle.width = 24; wrapped.setStyle(wordStyle);
    wrapped.setText("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9");
    doc.render(180,100); require(wrapped.bounds().height > 35,"UTF-8 long tokens wrap without byte splitting");
    auto unicodePixels = doc.render(180,100).pixels;
    wrapped.setText("\xc3\xa9\xc3\xa9\n\xc3\xa9\xc3\xa9\n\xc3\xa9\xc3\xa9");
    require(unicodePixels == doc.render(180,100).pixels,"UTF-8 wrapping retains complete glyphs");
    doc.root().clear();
    Style panel; panel.width = 100; panel.height = 40; panel.shrink = 0;
    auto& clipped = doc.root().column().setStyle(panel);
    int clicks = 0;
    auto& child = clipped.button("Overflow",[&] { ++clicks; });
    auto style = child.style(); style.width = 220; style.height = 80; style.shrink = 0;
    style.background = {255,0,0,255}; child.setStyle(style);
    const auto& surface = doc.render(400,200);
    require(surface.pixels[20*400+150] == 0,"overflow visually clipped");
    require(!doc.pointerDown(150,20),"clipped overflow cannot receive input");
    doc.pointerUp(150,20); require(clicks == 0,"clipped control cannot activate");
    clipped.setEnabled(false); doc.pointerDown(20,20); doc.pointerUp(20,20);
    require(clicks == 0,"disabled ancestor disables descendants");
    clipped.setEnabled(true); doc.pointerDown(20,20); clipped.setVisible(false); doc.pointerUp(20,20);
    require(clicks == 0,"hidden ancestor cancels capture");
}
void views(const std::filesystem::path& output) {
    LauncherView launcher("No project is open. Choose a folder or drop a supported file.",[]{},[]{});
    auto& full = launcher.render(1000,680);
    require(full.pixels[0] == 0xff080b12,"launcher background");
    if (!output.empty()) save(full,output/"launcher.bmp");
    auto& narrow = launcher.render(380,640);
    if (!output.empty()) save(narrow,output/"launcher-narrow.bmp");
    require(launcher.document().keyDown(Key::Tab),"launcher has keyboard controls");
    launcher.setBusy(true); require(!launcher.document().keyDown(Key::Tab),"busy launcher disables actions");
    // Shared runtime must survive one document's destruction while another lives.
    { Document temporary; temporary.root().label("Other view"); temporary.render(100,40); }
    launcher.setBusy(false); launcher.render(1000,680);
    RendererHud hud; RendererHudInfo info;
    info.backend = "D3D11"; info.quality = "medium"; info.debugView = "none";
    info.viewportWidth = 1280; info.viewportHeight = 720; info.gpuMs = 9.42; info.cpuMs = 4.12; info.draws = 16;
    const auto& rendered = hud.render(1280,info);
    require(rendered.height == 118 && rendered.pixels[0] == 0,"HUD retains transparent upload format");
    if (!output.empty()) save(rendered,output/"hud.bmp");
    hud.render(380,info); hud.render(1,info);
}
void editorLayout(const std::filesystem::path& output) {
    // Toolkit composition example, not an implemented Genesis editor.
    Document doc;
    Style root; root.padding = 16; root.gap = 12; root.background = {9,13,21,255}; doc.root().setStyle(root);
    auto& toolbar = doc.root().row(); auto bar = toolbar.style(); bar.gap = 12; bar.align = Align::Center;
    toolbar.setStyle(bar);
    Style heading; heading.font = Font::Semibold; heading.fontSize = 15; heading.grow = 1;
    toolbar.label("GENESIS   /   UI toolkit").setStyle(heading);
    bool running = false;
    auto& play = toolbar.button("Preview",[]{});
    play.onClick([&] { running = !running; play.setText(running ? "Stop preview" : "Preview"); });
    auto& workspace = doc.root().row(); auto row = workspace.style(); row.grow = 1; row.gap = 12;
    workspace.setStyle(row);
    Style panel; panel.width = 220; panel.padding = 16; panel.gap = 12;
    panel.radius = 12; panel.background = {22,29,42,255};
    auto& scene = workspace.column().setStyle(panel); scene.label("SCENE");
    Style viewport = panel; viewport.width = {}; viewport.grow = 1;
    viewport.align = Align::Center; viewport.justify = Justify::Center; viewport.background = {13,19,30,255};
    auto& center = workspace.column().setStyle(viewport);
    center.label("Viewport host"); center.label("Renderer output can be composited here.");
    panel.width = 260;
    auto& inspector = workspace.column().setStyle(panel); inspector.label("INSPECTOR");
    auto& selected = inspector.label("Nothing selected");
    auto& camera = scene.button("Camera",[&] { selected.setText("Camera selected"); });
    scene.button("Point light",[&] { selected.setText("Point light selected"); });
    scene.button("Mesh",[&] { selected.setText("Mesh selected"); });
    scene.button("Unavailable action",[]{}).setEnabled(false);
    inspector.label("Layout, state, input and vector drawing");
    inspector.label("No renderer or window dependency");
    doc.root().label("EDITOR LAYOUT EXAMPLE   /   Yoga + ThorVG   /   C++ API");
    doc.layout(1200,700);
    near(scene.bounds().width,220,"editor sidebar width"); near(inspector.bounds().width,260,"inspector width");
    require(center.bounds().width > 600,"editor viewport fills remaining space");
    const auto b = camera.bounds(); doc.pointerDown(b.x+10,b.y+10); doc.pointerUp(b.x+10,b.y+10);
    require(selected.text() == "Camera selected","selection updates inspector independently of renderer");
    if (!output.empty()) save(doc.render(1200,700),output/"editor-layout.bmp");
}
void editorControls(const std::filesystem::path& output) {
    Document doc; Style root; root.padding = 10; root.gap = 8; doc.root().setStyle(root);
    int changes = 0; bool checked = false; float number = 5, slider = 0;
    std::string changed;
    auto& input = doc.root().textField("Hello",[&](const std::string& value) { changed = value; ++changes; });
    auto& numeric = doc.root().numberField(5,0,100,.5f,[&](float value) { number = value; ++changes; });
    auto& check = doc.root().checkBox("Enabled",false,[&](bool value) { checked = value; ++changes; });
    auto& range = doc.root().slider(0,0,10,1,[&](float value) { slider = value; ++changes; });
    doc.render(400,300); doc.keyDown(Key::Tab);
    require(input.focused() && doc.wantsTextInput(),"text fields focus and request platform text input");
    require(doc.selectedText() == "Hello","tab selects field contents");
    doc.textInput("World"); require(input.text() == "World" && changed == "World","typing replaces selection and binds value");
    doc.keyDown(Key::Left); doc.keyDown(Key::Backspace); require(input.text() == "Word","caret backspace edits correct position");
    doc.keyDown(Key::Home); doc.keyDown(Key::Right,true); require(doc.selectedText() == "W","shift selection");
    doc.cutSelection(); require(input.text() == "ord","cut selected text");
    doc.keyDown(Key::SelectAll); doc.textInput("\xc3\xa9\xc3\xa9");
    doc.keyDown(Key::Backspace); require(input.text() == "\xc3\xa9","backspace preserves UTF-8 codepoints");
    doc.keyDown(Key::Tab); require(numeric.focused(),"number focus");
    doc.textInput("27.5"); require(number == 5,"numeric draft does not mutate committed value");
    doc.keyDown(Key::Enter); require(number == 27.5f,"numeric commit");
    doc.keyDown(Key::SelectAll); doc.textInput("bad"); doc.keyDown(Key::Enter);
    require(number == 27.5f && numeric.text() == "bad","invalid numeric draft rejected");
    doc.keyDown(Key::Tab); require(numeric.text() == "27.5","invalid number reverts on blur");
    require(check.focused(),"checkbox focus"); doc.keyDown(Key::Space); doc.keyUp(Key::Space);
    require(checked && check.checked(),"checkbox keyboard toggles bound value");
    doc.keyDown(Key::Tab); doc.keyDown(Key::Right); require(slider == 1,"slider keyboard step");
    doc.keyDown(Key::End); require(slider == 10,"slider end bound");
    auto bounds = range.bounds();
    doc.pointerDown(bounds.x+10,bounds.y+10); doc.pointerMove(bounds.x+bounds.width*.5f,bounds.y+10);
    doc.pointerUp(bounds.x+bounds.width*.5f,bounds.y+10); require(slider == 5,"slider drag and quantization");
    doc.pointerDown(bounds.x+10,bounds.y+10); range.setEnabled(false); doc.pointerMove(500,0); doc.pointerUp(500,0);
    require(slider == 0,"disabling captured slider cancels dragging");
    auto n = numeric.bounds(); doc.pointerDown(n.x+10,n.y+10); doc.pointerUp(n.x+10,n.y+10);
    doc.keyDown(Key::SelectAll); doc.textInput("999"); doc.keyDown(Key::Enter); require(number == 100,"number clamps range");
    doc.keyDown(Key::Down); require(number == 99.5f,"number arrow stepping");
    doc.keyDown(Key::SelectAll); doc.textInput("-50"); doc.keyDown(Key::Escape);
    require(numeric.text() == "99.5" && number == 99.5f,"escape cancels numeric draft");
    const int count = changes; numeric.setValue(12); check.setChecked(false); range.setValue(7);
    require(changes == count,"programmatic setters do not emit feedback loops");
    if (!output.empty()) save(doc.render(400,300),output/"controls.bmp");

    Document scrollDoc; ScrollView scroll(scrollDoc.root());
    auto style = scroll.viewport().style(); style.height = 150; style.grow = 0; scroll.viewport().setStyle(style);
    for (int i = 0; i < 20; ++i) scroll.content().button("Row "+std::to_string(i),[]{});
    scrollDoc.layout(300,200);
    require(scrollDoc.wheel(20,20,100) && scroll.viewport().scrollOffset() == 100,"scroll wheel moves overflowing panel");
    scrollDoc.wheel(20,20,10000); require(scroll.viewport().scrollOffset() < 10000,"scroll clamp");
    scroll.viewport().scrollTo(0); scrollDoc.layout(300,200);
    for (int i = 0; i < 10; ++i) scrollDoc.keyDown(Key::Tab);
    require(scroll.viewport().scrollOffset() > 0,"tab reveals controls beyond scroll viewport");

    Document treeDoc; Tabs tabs(treeDoc.root());
    auto& first = tabs.add("Scene"); auto& second = tabs.add("Files");
    TreeView tree(first,[](const std::string&){});
    tree.setItems({{"root","Scene",{{"camera","Camera"},{"light","Light"}}}});
    tree.select("light"); treeDoc.layout(400,300); require(tree.selected() == "light","tree selection state");
    tree.setExpanded("root",false); treeDoc.layout(400,300); require(tree.selected() == "light","tree collapse retains selection");
    second.label("Files"); tabs.activate(1); treeDoc.layout(400,300);
    require(tabs.active() == 1 && first.bounds().height == 0 && second.bounds().height > 0,"tabs switch real content");
    bool rejected = false;
    try { tree.setItems({{"same","One"},{"same","Two"}}); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected,"tree rejects duplicate IDs");

    genesis::editor::UiWorkbench workbench;
    const auto& surface = workbench.render(1280,820);
    if (!output.empty()) save(surface,output/"workbench.bmp");
    workbench.render(1000,680); workbench.select(1);
    require(workbench.selectedText() == "Edit this label in the inspector","workbench selection binds inspector model");
}
void draggableNumberField() {
    Document doc;
    float value=1.0f;int changes=0;
    auto& field=doc.root().numberField(value,-100,100,.1f,[&](float next){
        value=next;++changes;
    }).setDragAdjustable().setNumberDecimals(3);
    Style style;style.width=140;style.height=24;field.setStyle(style);
    doc.layout(200,60);
    const auto bounds=field.bounds();const float x=bounds.x+20,y=bounds.y+12;
    doc.pointerMove(x,y);
    require(doc.cursor()==Cursor::ResizeHorizontal,"numeric hover advertises horizontal adjustment");
    doc.pointerDown(x,y);doc.pointerUp(x,y);
    require(field.focused() && doc.selectedText()=="1.000","numeric click selects value for typing");
    doc.textInput("2.5");doc.keyDown(Key::Enter);
    require(std::abs(value-2.5f)<.001f,"numeric click accepts typed value");
    doc.pointerDown(x,y);doc.pointerMove(x+3,y);
    require(std::abs(value-2.5f)<.001f,"small pointer movement keeps text editing");
    doc.pointerMove(x+30,y);
    require(std::abs(value-2.8f)<.001f && changes>=2,"horizontal drag changes numeric value live");
    doc.pointerMove(x+40,y);doc.pointerUp(x+40,y);
    require(std::abs(value-2.9f)<.001f && !field.focused(),"drag ends without leaving text editor active");
    doc.pointerMove(190,50);
    require(doc.cursor()==Cursor::Arrow,"numeric cursor clears after leaving field");
}
void editingAndNavigation() {
    Document doc; int commits=0; std::string committed;
    auto& field=doc.root().textField("",{});
    field.onTextCommitted([&](const std::string& value){ ++commits; committed=value; });
    const auto blank=doc.render(300,100).pixels;
    field.setPlaceholder("Search objects...");
    require(field.text().empty() && blank!=doc.render(300,100).pixels,"placeholder paints without changing text value");
    field.setText("Before"); field.focus(); doc.textInput("After"); doc.textInput(" typing");
    require(commits==0,"text commits wait for Enter or blur");
    doc.keyDown(Key::Enter); require(commits==1 && committed=="After typing","typing becomes one committed edit");
    doc.cancelInput(); require(commits==1,"blur after Enter does not duplicate commit");
    field.focus(); doc.textInput("Cancel me"); doc.keyDown(Key::Escape);
    require(commits==1 && field.text()=="After typing","Escape restores committed text");
    field.focus(); doc.textInput("Blur edit"); doc.cancelInput();
    require(commits==2 && committed=="Blur edit","focus loss commits draft");
    field.setText("Programmatic"); field.focus(); doc.cancelInput(); require(commits==2,"programmatic text does not commit");
    auto& sibling=doc.root().column(); int clicks=0;
    auto& save=doc.root().button("Save",[&]{++clicks;});
    field.onTextCommitted([&](const std::string&){sibling.clear();sibling.label("Updated");});
    field.focus(); doc.textInput("Changed"); doc.layout(300,160); const auto saveBox=save.bounds();
    doc.pointerDown(saveBox.x+10,saveBox.y+10); doc.pointerUp(save.bounds().x+10,save.bounds().y+10);
    require(clicks==1,"blur commit that rebuilds a sibling keeps the clicked control focused");
    field.onTextCommitted([&](const std::string&){ doc.root().clear(); });
    field.focus(); doc.textInput("Remove me"); doc.keyDown(Key::Enter); doc.render(300,100);
    require(!doc.wantsTextInput(),"commit callback may remove its field");

    Document scrolling; ScrollView scroll(scrolling.root());
    auto s=scroll.viewport().style(); s.height=120; s.grow=0; scroll.viewport().setStyle(s);
    for(int i=0;i<20;++i) scroll.content().button("Scrollable row",[]{});
    scrolling.layout(300,180);
    require(scrolling.pointerDown(295,8),"scrollbar thumb accepts pointer capture");
    scrolling.pointerMove(500,90);
    require(scroll.viewport().scrollOffset()>300,"scrollbar drags outside track with capture");
    scrolling.cancelInput(); const auto offset=scroll.viewport().scrollOffset(); scrolling.pointerMove(295,10);
    near(scroll.viewport().scrollOffset(),offset,"focus loss cancels scrollbar drag");
    scrolling.pointerDown(295,10); scrolling.pointerUp(295,10);
    require(scroll.viewport().scrollOffset()<offset,"clicking track moves scrollbar");
    scrolling.pointerDown(295,10); scroll.viewport().setEnabled(false); const auto disabled=scroll.viewport().scrollOffset();
    scrolling.pointerMove(295,100); scrolling.pointerUp(295,100);
    near(scroll.viewport().scrollOffset(),disabled,"disabling scroll viewport releases capture");
    scroll.viewport().setEnabled(true); scrolling.pointerDown(295,10); scrolling.root().clear();
    scrolling.pointerMove(295,100); scrolling.pointerUp(295,100); scrolling.render(300,180);
    require(!scrolling.pointerDown(295,10),"destroying captured scroll viewport is safe");

    Document hierarchy; int selections=0; TreeView tree(hierarchy.root(),[&](const std::string&){++selections;});
    tree.setItems({{"root","Scene",{{"one","One"},{"two","Two"}}}});
    hierarchy.layout(300,180); tree.select("root"); tree.focusSelected();
    hierarchy.keyDown(Key::Down); require(tree.selected()=="one" && selections==1,"tree Down selects and emits");
    hierarchy.keyDown(Key::End); require(tree.selected()=="two","tree End selects last visible row");
    hierarchy.keyDown(Key::Left); require(tree.selected()=="root","tree Left selects parent");
    hierarchy.keyDown(Key::Left); hierarchy.keyDown(Key::Down);
    require(tree.selected()=="root","collapsed children excluded from navigation");
    tree.setItems({{"root","Renamed scene",{{"one","One"},{"two","Two"}}}});
    hierarchy.keyDown(Key::Down); require(tree.selected()=="root","tree updates preserve expansion and focus");
    hierarchy.keyDown(Key::Right); hierarchy.keyDown(Key::Right);
    require(tree.selected()=="one","tree Right expands then enters first child");
    hierarchy.keyDown(Key::Up); require(tree.selected()=="root","tree Up moves to preceding visible row");

    Document panels; auto row=panels.root().style(); row.direction=Direction::Row; panels.root().setStyle(row);
    Style fixed; fixed.width=100; fixed.shrink=0; auto& left=panels.root().column().setStyle(fixed);
    auto& splitter=panels.root().splitter(Direction::Row,[&](float delta){auto s=left.style();s.width=s.width.value+delta;left.setStyle(s);});
    Style flexible; flexible.grow=1; panels.root().column().setStyle(flexible); panels.layout(400,200);
    panels.pointerMove(102,50);require(panels.cursor()==Cursor::ResizeHorizontal,"vertical divider advertises horizontal resizing");
    panels.pointerDown(102,50); panels.pointerMove(162,50);panels.pointerLeave();
    require(panels.cursor()==Cursor::ResizeHorizontal,"splitter keeps resize cursor during captured drag outside the window");
    panels.pointerUp(162,50); panels.layout(400,200);
    near(left.bounds().width,160,"splitter pointer drag changes Yoga panel width");
    const auto& afterRelease=panels.renderGpu(400,200);
    const auto dividerBounds=splitter.bounds();
    const auto divider=std::find_if(afterRelease.commands.begin(),afterRelease.commands.end(),[&](const auto& command){
        return command.type==GpuDrawCommand::Type::RoundedRect &&
            command.bounds.x==dividerBounds.x && command.bounds.y==dividerBounds.y &&
            command.bounds.width==dividerBounds.width && command.bounds.height==dividerBounds.height;
    });
    require(divider!=afterRelease.commands.end() && divider->borderWidth==0 && divider->fill.a==255,
        "released splitter stays opaque without a focus-colored stroke");
    panels.keyDown(Key::Right); panels.layout(400,200); near(left.bounds().width,170,"focused splitter supports keyboard resize");
    panels.pointerDown(172,50); panels.cancelInput(); panels.pointerMove(250,50); panels.layout(400,200);
    near(left.bounds().width,170,"focus loss releases splitter capture");
    require(panels.cursor()==Cursor::Arrow,"released splitter restores arrow over panel content");
    splitter.setEnabled(false); require(!panels.pointerDown(172,50),"disabled splitter does not capture");
    require(panels.cursor()==Cursor::Arrow,"disabled splitter does not advertise resizing");
    splitter.setEnabled(true);panels.pointerMove(172,50);panels.cancelInput();
    require(panels.cursor()==Cursor::Arrow,"focus loss clears splitter hover cursor");
    panels.pointerMove(172,50);splitter.setVisible(false);
    require(panels.cursor()==Cursor::Arrow,"hidden splitter cannot leave a stale resize cursor");
}
void menusAndIcons(const std::filesystem::path& output) {
    Document doc;int saved=0,edited=0,underlying=0;
    auto& bar=doc.root().row();auto barStyle=bar.style();barStyle.height=24;barStyle.shrink=0;bar.setStyle(barStyle);
    auto& body=doc.root().button("Underlying control",[&]{++underlying;});
    auto fill=body.style();fill.grow=1;body.setStyle(fill);
    MenuBar menus(doc,bar);
    menus.add("File",{{"Save","Ctrl+S",[&]{++saved;}},{"Disabled","",[]{},[]{return false;}}});
    menus.add("Edit",{{"Command","",[&]{++edited;}}});
    doc.render(320,200);doc.keyDown(Key::Tab);doc.keyDown(Key::Down);
    require(doc.hasPopup(),"Down on menu opens Yoga popup");
    if(!output.empty())save(doc.render(320,200),output/"menu-popup.bmp");
    doc.keyDown(Key::Tab);doc.keyDown(Key::Enter);doc.keyUp(Key::Enter);
    require(saved==1 && !doc.hasPopup(),"popup focus excludes disabled rows and underlying controls");
    doc.keyDown(Key::Down);doc.keyDown(Key::Right);doc.keyDown(Key::Enter);doc.keyUp(Key::Enter);
    require(edited==1 && !doc.hasPopup(),"menu keyboard navigation switches menus and invokes action");
    doc.keyDown(Key::Down);doc.keyDown(Key::Escape);require(!doc.hasPopup(),"Escape dismisses popup");
    doc.keyDown(Key::Down);doc.pointerDown(300,190);doc.pointerUp(300,190);
    require(!doc.hasPopup() && underlying==0,"outside click dismisses without activating underlying UI");
    doc.keyDown(Key::Down);doc.cancelInput();require(!doc.hasPopup(),"focus loss closes popup");
    doc.root().clear();auto& icon=doc.root().label("").setIcon(Icon::Cube,40);
    const auto pixels=doc.render(80,80).pixels;
    require(std::any_of(pixels.begin(),pixels.end(),[](uint32_t p){
        return ((p>>16)&255)>200 && ((p>>8)&255)>150 && (p&255)<150;
    }),"Godot BoxMesh SVG rasterizes in its original color");
    require(icon.bounds().height>=40,"Yoga measures vector icon size");
    icon.setGodotIcon("Folder",40);
    require(std::any_of(doc.render(80,80).pixels.begin(),doc.render(80,80).pixels.end(),[](uint32_t p){return p!=0;}),
        "named Godot editor icon renders");
    icon.setGodotIcon("modules/csg/icons/CSGBox3D",40);
    require(std::any_of(doc.render(80,80).pixels.begin(),doc.render(80,80).pixels.end(),[](uint32_t p){return p!=0;}),
        "Godot module editor icon renders");
    bool rejected=false;try{icon.setGodotIcon("../Folder");}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Godot icon names cannot escape the icon library");
    const auto iconRoot=std::filesystem::path(GENESIS_ROOT)/"assets/editor_icons/godot";
    size_t iconCount=0;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(iconRoot)) {
        if(!entry.is_regular_file() || entry.path().extension()!=".svg")continue;
        auto name=std::filesystem::relative(entry.path(),iconRoot).generic_string();name.resize(name.size()-4);
        try { icon.setGodotIcon(name,16);doc.render(80,80); }
        catch(const std::exception& error) { throw std::runtime_error("Godot icon "+name+": "+error.what()); }
        ++iconCount;
    }
    require(iconCount==1066,"all pinned Godot editor SVG icons are available to the toolkit");
}
void dropdownWidget() {
    Document doc;size_t changes=0,chosen=0;
    auto& bar=doc.root().row();auto style=bar.style();style.height=27;bar.setStyle(style);
    Dropdown dropdown(doc,bar,{{"Global","orientation_global"},{"Local","orientation_local"},
        {"Unavailable","",false}},0,[&](size_t index){++changes;chosen=index;});
    doc.render(320,180);dropdown.button().focus();doc.keyDown(Key::Down);
    require(doc.hasPopup(),"dropdown opens from keyboard");
    doc.keyDown(Key::Down);doc.keyDown(Key::Enter);doc.keyUp(Key::Enter);
    require(!doc.hasPopup() && dropdown.selected()==1 && chosen==1 && changes==1,
        "dropdown selects and emits once");
    dropdown.open();doc.keyDown(Key::Down);doc.keyDown(Key::Down);
    doc.keyDown(Key::Enter);doc.keyUp(Key::Enter);
    require(dropdown.selected()==0 && chosen==0 && changes==2,
        "dropdown keyboard navigation skips disabled choices");
}
void popupTooltips() {
    Document doc;
    auto& button=doc.root().button("",[]{}).setTooltip("Show Gizmo","Show transform gizmos.");
    auto style=button.style();style.width=30;style.height=25;button.setStyle(style);
    const auto base=doc.renderGpu(300,150).commands.size();
    doc.pointerMove(12,12);
    require(doc.renderGpu(300,150).commands.size()==base,"tooltip waits before appearing");
    std::this_thread::sleep_for(std::chrono::milliseconds(530));
    const auto& shown=doc.renderGpu(300,150);
    require(shown.commands.size()>base,"hover tooltip draws in GPU UI");
    const auto revision=shown.revision;
    require(doc.renderGpu(300,150).revision==revision,"visible tooltip preserves GPU cache");
    doc.pointerDown(12,12);
    require(doc.renderGpu(300,150).commands.size()==base,"tooltip closes on pointer press");
    doc.pointerUp(12,12);doc.pointerLeave();
    require(doc.renderGpu(300,150).commands.size()==base,"tooltip stays hidden after pointer leaves");
}
void resourceAndColor(const std::filesystem::path& output) {
    Document doc;auto& anchor=doc.root().button("Pick",[]{});ResourcePicker picker(doc);doc.layout(460,400);
    std::string chosen;int calls=0;
    auto open=[&]{picker.open(anchor,"Models",{{"one","Cube","A mesh",Icon::Cube},{"two","Disabled","Unavailable",Icon::File,false}},[&](const std::string& id){chosen=id;++calls;},true);};
    open();require(doc.wantsTextInput(),"resource picker focuses search");doc.textInput("disabled");doc.keyDown(Key::Enter);
    require(calls==0 && doc.hasPopup(),"disabled resource cannot be chosen through search");
    doc.keyDown(Key::SelectAll);doc.textInput("cube");doc.keyDown(Key::Down);doc.keyDown(Key::Enter);doc.keyUp(Key::Enter);
    require(chosen=="one" && calls==1 && !doc.hasPopup(),"filtered resource keyboard choice dismisses popup once");
    open();doc.keyDown(Key::Escape);require(calls==1 && !doc.hasPopup() && anchor.focused(),"resource cancellation restores anchor focus");
    Document colors;int changes=0;std::array<float,3> selected{};
    ColorField field(colors.root(),[&](auto color){++changes;selected=color;});field.setValue({.25f,.5f,.75f});
    colors.render(300,90);if(!output.empty())save(colors.render(300,90),output/"color-field-hex.bmp");
    colors.keyDown(Key::Tab);colors.keyDown(Key::SelectAll);
    require(changes==0 && colors.selectedText()=="#3F7FBF","silent color update displays hex value");
    colors.textInput("#336699");colors.keyDown(Key::Enter);
    require(changes==1 && std::abs(selected[0]-.2f)<.001f && std::abs(selected[1]-.4f)<.001f,
        "hex color commit emits complete color once");
    colors.keyDown(Key::SelectAll);colors.textInput("#zzzzzz");colors.keyDown(Key::Enter);
    colors.keyDown(Key::SelectAll);
    require(changes==1 && colors.selectedText()=="#336699","invalid hex restores previous color");
    colors.keyDown(Key::Tab);colors.keyDown(Key::Tab);colors.keyDown(Key::SelectAll);colors.textInput("0.6");colors.keyDown(Key::Enter);
    require(changes==2 && selected[0]==.6f && std::abs(selected[1]-.4f)<.001f,
        "RGB number commit updates hex color");
    Document pickerDoc;int picked=0;std::array<float,3> pickedColor{};
    ColorField pickedField(pickerDoc.root(),[&](auto color){++picked;pickedColor=color;});
    ColorPicker colorPicker(pickerDoc);pickedField.setPicker(colorPicker);
    pickerDoc.render(330,360);pickerDoc.pointerDown(310,10);pickerDoc.pointerUp(310,10);
    require(pickerDoc.hasPopup(),"Pick button opens color picker");
    if(!output.empty())save(pickerDoc.render(330,360),output/"color-picker.bmp");
    pickerDoc.keyDown(Key::Tab);pickerDoc.keyDown(Key::Tab);
    pickerDoc.keyDown(Key::SelectAll);pickerDoc.textInput("#336699");pickerDoc.keyDown(Key::Enter);
    require(picked==1 && std::abs(pickedColor[0]-0.2f)<.001f &&
        std::abs(pickedColor[1]-0.4f)<.001f && std::abs(pickedColor[2]-0.6f)<.001f,
        "hex entry emits chosen color once");
    pickerDoc.keyDown(Key::Escape);require(!pickerDoc.hasPopup(),"Escape closes color picker");
    pickerDoc.pointerDown(310,10);pickerDoc.pointerUp(310,10);pickerDoc.render(330,360);
    const auto beforeSquare=pickedColor;
    pickerDoc.pointerDown(120,160);pickerDoc.pointerUp(120,160);
    require(picked==2 && pickedColor!=beforeSquare,"continuous color square accepts click");
    const auto beforeHue=pickedColor;
    pickerDoc.pointerDown(253,160);pickerDoc.pointerUp(253,160);
    require(picked==3 && pickedColor!=beforeHue,"hue strip changes selected color");
    pickerDoc.pointerDown(120,160);pickerDoc.pointerMove(200,90);pickerDoc.pointerUp(200,90);
    require(picked>=5 && !pickerDoc.hasPointerCapture(),"color square supports drag and releases capture");
}
void docking(const std::filesystem::path& output) {
    Document doc;int changes=0;DockSpace dock(doc,doc.root(),[&]{++changes;});
    auto& a=dock.add("a","Scene",Icon::Scene,180,120);
    auto& field=a.textField("Preserved inspector value",{});
    dock.add("b","Inspector",Icon::File,160,120).label("Inspector content");
    dock.add("c","Console",Icon::Console,160,100).label("Console content");
    auto initial=DockLayout::split(Direction::Row,.65f,DockLayout::group({"a"}),DockLayout::group({"b","c"}));
    require(dock.setLayout(initial),"valid dock tree mounts");
    const auto layout=[&](float width=800,float height=500){doc.layout(width,height);dock.arrange();doc.layout(width,height);};
    layout();require(dock.visible("a") && dock.visible("b") && !dock.visible("c"),"only active pages are visible");
    const auto& framed=doc.render(800,500);
    if(!output.empty())save(framed,output/"dock-gutter.bmp");
    require(framed.pixels[0]==0xff202020 && framed.pixels[499*800+799]==0xff202020,
        "rounded dock frame covers what lies outside its corners");
    require((framed.pixels[499*800+100]>>24)==255 && (framed.pixels[100*800]>>24)==255,
        "dock frame border paints above page content");
    const auto editorButton=dock.editorButtonBounds("a");
    require(editorButton.width>0,"area header exposes an editor type selector");
    doc.pointerDown(editorButton.x+editorButton.width*.5f,editorButton.y+editorButton.height*.5f);
    doc.pointerUp(editorButton.x+editorButton.width*.5f,editorButton.y+editorButton.height*.5f);
    require(doc.hasPopup(),"editor type selector opens its choices");
    if(!output.empty())save(doc.render(800,500),output/"dock-editor-menu.bmp");
    doc.keyDown(Key::Down);doc.keyDown(Key::Enter);doc.keyUp(Key::Enter);layout();
    require(!doc.hasPopup() && dock.visible("b") && dock.visible("a"),
        "choosing an open editor swaps the two areas without losing either panel");
    dock.setLayout(initial);layout();
    require(dock.selectEditor("a","c") && dock.visible("c") && dock.open("a"),
        "editor type selection can swap with a background tab");
    dock.setLayout(initial);layout();
    dock.close("c");layout();
    require(dock.selectEditor("a","c") && dock.visible("c") && !dock.open("a"),
        "choosing a closed editor replaces the current area and keeps it available in the menu");
    dock.setLayout(initial);layout();
    auto rect=dock.tabBounds("c");doc.pointerDown(rect.x+15,rect.y+10);doc.pointerUp(rect.x+15,rect.y+10);
    require(dock.visible("c") && !dock.visible("b"),"tab click activates page");
    doc.keyDown(Key::Left);require(dock.visible("b"),"dock tabs support keyboard activation");
    auto destination=dock.panelBounds("a");rect=dock.tabBounds("c");
    doc.pointerDown(rect.x+15,rect.y+10);doc.pointerMove(destination.x+destination.width*.5f,250);
    require(dock.dragging() && doc.hasPointerCapture(),"tab drag captures across other pages");
    doc.keyDown(Key::Tab);doc.keyDown(Key::Enter);doc.keyUp(Key::Enter);doc.wheel(200,200,40);
    require(dock.dragging() && doc.hasPointerCapture() && dock.visible("b"),"keyboard activation and scrolling cannot interrupt active tab drag");
    if(!output.empty())save(doc.render(800,500),output/"dock-drop-preview.bmp");
    doc.keyDown(Key::Escape);require(!dock.dragging() && dock.visible("b") && !doc.hasPointerCapture(),"Escape cancels dock drag without changing tabs");
    doc.pointerDown(rect.x+15,rect.y+10);doc.pointerMove(-20,-20);doc.pointerUp(-20,-20);
    require(dock.visible("b") && !dock.dragging(),"drop outside workspace leaves layout intact");
    doc.pointerDown(rect.x+15,rect.y+10);doc.pointerMove(destination.x+destination.width*.5f,250);doc.cancelInput();
    require(!dock.dragging() && dock.visible("b"),"focus loss cancels tab drag");
    doc.pointerDown(rect.x+15,rect.y+10);doc.pointerMove(destination.x+destination.width*.5f,250);doc.pointerUp(destination.x+destination.width*.5f,250);layout();
    require(dock.visible("c") && !dock.visible("a") && dock.visible("b"),"center drop creates tab group and activates dropped tab");
    require(field.text()=="Preserved inspector value","moving panels preserves descendant state and references");
    // Header drops place tabs at the indicated insertion marker.
    auto from=dock.tabBounds("c"),to=dock.tabBounds("a");
    doc.pointerDown(from.x+10,from.y+10);doc.pointerMove(to.x+1,to.y+10);doc.pointerUp(to.x+1,to.y+10);layout();
    require(dock.layout().children[0].tabs==std::vector<std::string>{"c","a"},"drag reorders tabs within a group");
    for(auto edge:{DockEdge::Left,DockEdge::Right,DockEdge::Top,DockEdge::Bottom}) {
        dock.setLayout(initial);layout();auto target=dock.panelBounds("a");auto source=dock.tabBounds("c");
        float x=target.x+target.width*.5f,y=target.y+target.height*.5f;
        if(edge==DockEdge::Left)x=target.x+3;if(edge==DockEdge::Right)x=target.x+target.width-3;
        if(edge==DockEdge::Top){const auto tab=dock.tabBounds("a");y=tab.y+tab.height+2;}
        if(edge==DockEdge::Bottom)y=target.y+target.height-3;
        doc.pointerDown(source.x+10,source.y+10);doc.pointerMove(x,y);doc.pointerUp(x,y);layout();
        auto placed=dock.panelBounds("c"),other=dock.panelBounds("a");
        require(edge==DockEdge::Left?placed.x<other.x:edge==DockEdge::Right?placed.x>other.x:edge==DockEdge::Top?placed.y<other.y:placed.y>other.y,"edge drop creates requested split");
    }
    dock.setLayout(initial);layout();auto before=dock.panelBounds("a");
    doc.pointerDown(before.width+2,250);
    doc.pointerMove(before.width+12,250);
    doc.pointerMove(before.width+22,250);
    doc.pointerMove(before.width+42,250);
    doc.pointerUp(before.width+42,250);layout();
    near(dock.panelBounds("a").width,before.width+40,"dock splitter resizes pane");
    doc.keyDown(Key::Left);layout();near(dock.panelBounds("a").width,before.width+30,"dock splitter keyboard resize");
    layout(300,200);require(dock.panelBounds("a").width>0 && dock.panelBounds("b").x+dock.panelBounds("b").width<=300,"overconstrained layout stays within small window");
    auto invalid=initial;invalid.children[1].tabs.push_back("a");
    require(!dock.setLayout(invalid) && dock.visible("a"),"duplicate panel ID rejected atomically");
    invalid=initial;invalid.ratio=NAN;require(!dock.setLayout(invalid),"nonfinite split rejected");
    invalid=initial;invalid.children[1].active="missing";require(!dock.setLayout(invalid),"missing active tab rejected");
    invalid=initial;invalid.children[1]=DockLayout::group({"unknown"});require(!dock.setLayout(invalid),"unknown panel ID rejected");
    dock.setLayout(initial);layout();
    auto splitSource=dock.panelBounds("a");
    const float cornerX=splitSource.x+splitSource.width-4,cornerY=splitSource.y+4;
    doc.pointerMove(cornerX,cornerY);
    require(doc.cursor()==Cursor::Crosshair,"rounded editor corner advertises splitting");
    doc.pointerDown(cornerX,cornerY);
    doc.pointerMove(cornerX-170,cornerY+2);
    require(dock.dragging() && doc.hasPointerCapture(),"corner split keeps pointer capture during drag");
    if(!output.empty())save(doc.render(800,500),output/"dock-corner-preview.bmp");
    doc.pointerUp(cornerX-170,cornerY+2);layout();
    require(dock.layout().children[0].children.size()==2,
        "corner drag creates a persistent split area");
    if(!output.empty())save(doc.render(800,500),output/"dock-corner-split.bmp");
    const auto splitTarget=dock.layout().children[0].children[1].tabs.front();
    const auto newEditorButton=dock.editorButtonBounds(splitTarget);
    require(newEditorButton.width>0,"new split area exposes the editor type menu");
    require(dock.dock("c",splitTarget,DockEdge::Center),"editor tab can fill a corner-split area");
    layout();
    require(!dock.open(splitTarget) && dock.visible("c"),"filling a split removes its empty placeholder");
    dock.setLayout(initial);layout();
    splitSource=dock.panelBounds("a");
    const float bottomX=splitSource.x+4,bottomY=splitSource.y+splitSource.height-4;
    doc.pointerMove(bottomX,bottomY);
    require(doc.cursor()==Cursor::Crosshair,"bottom corner also advertises splitting");
    doc.pointerDown(bottomX,bottomY);doc.pointerMove(bottomX+2,bottomY-170);
    doc.keyDown(Key::Escape);layout();
    require(dock.layout().children[0].children.empty(),"Escape cancels corner split");
    doc.pointerMove(bottomX,bottomY);doc.pointerDown(bottomX,bottomY);
    doc.pointerMove(bottomX+2,bottomY-170);doc.pointerUp(bottomX+2,bottomY-170);layout();
    require(dock.layout().children[0].axis==Direction::Column &&
        dock.layout().children[0].children.size()==2,"vertical corner drag creates horizontal splitter");
    dock.setLayout(initial);layout();
    dock.show("c");layout();
    auto stacked=dock.panelBounds("c");
    doc.pointerMove(stacked.x+3,stacked.y+stacked.height-3);doc.pointerDown(stacked.x+3,stacked.y+stacked.height-3);
    doc.pointerMove(stacked.x+4,stacked.y+stacked.height-20);doc.pointerMove(stacked.x+4,stacked.y+stacked.height-150);
    doc.pointerUp(stacked.x+4,stacked.y+stacked.height-150);layout();
    require(dock.layout().children[1].children.size()==2 && dock.layout().children[1].axis==Direction::Column,
        "corner split follows the area after its active tab changes");
    near(dock.panelBounds("c").height,stacked.height-150-2,"split line follows the cursor");
    dock.setLayout(initial);layout();
    auto joinSource=dock.panelBounds("a");const float joinWidth=dock.panelBounds("b").x+dock.panelBounds("b").width-joinSource.x;
    doc.pointerMove(joinSource.x+joinSource.width-3,joinSource.y+3);
    doc.pointerDown(joinSource.x+joinSource.width-3,joinSource.y+3);
    doc.pointerMove(joinSource.x+joinSource.width-16,joinSource.y+12); // Enter the source first; see junction test.
    doc.pointerMove(joinSource.x+joinSource.width+10,joinSource.y+5);
    doc.pointerMove(joinSource.x+joinSource.width+60,joinSource.y+5);
    require(dock.dragging(),"corner drag outward previews a join");
    doc.pointerUp(joinSource.x+joinSource.width+60,joinSource.y+5);layout();
    require(dock.layout().children.empty() && dock.layout().tabs==std::vector<std::string>{"a"} &&
        !dock.open("b") && !dock.open("c"),"joining removes the neighboring area like Blender");
    near(dock.panelBounds("a").width,joinWidth,"joined area takes the neighbor's space");
    const auto stackedLayout=DockLayout::split(Direction::Row,.5f,DockLayout::group({"a"}),
        DockLayout::split(Direction::Column,.5f,DockLayout::group({"b"}),DockLayout::group({"c"})));
    dock.setLayout(stackedLayout);layout();
    joinSource=dock.panelBounds("a");
    const auto upper=dock.panelBounds("b"),lower=dock.panelBounds("c");
    doc.pointerMove(joinSource.x+joinSource.width-3,joinSource.y+3);
    doc.pointerDown(joinSource.x+joinSource.width-3,joinSource.y+3);
    doc.pointerMove(joinSource.x+joinSource.width-16,joinSource.y+12); // Enter the source first; see junction test.
    doc.pointerMove(upper.x+60,upper.y+upper.height*.5f);
    doc.pointerMove(lower.x+60,lower.y+lower.height*.5f);
    doc.pointerUp(lower.x+60,lower.y+lower.height*.5f);layout();
    require(!dock.open("c") && dock.open("b") && dock.open("a"),"one corner drag can retarget another area before release");
    near(dock.panelBounds("a").y,lower.y,"partial join splits off the misaligned part of the source");
    near(dock.panelBounds("a").width,lower.x+lower.width-joinSource.x,"partial join spans both areas");
    require(dock.panelBounds("b").width==upper.width,"areas outside a join keep their size");
    dock.setLayout(stackedLayout);layout();
    doc.pointerMove(joinSource.x+joinSource.width-3,joinSource.y+3);
    doc.pointerDown(joinSource.x+joinSource.width-3,joinSource.y+3);
    doc.pointerMove(joinSource.x+joinSource.width-16,joinSource.y+12); // Enter the source first; see junction test.
    doc.pointerMove(upper.x+60,upper.y+40);
    doc.pointerMove(joinSource.x+joinSource.width-150,joinSource.y+6);
    doc.pointerUp(joinSource.x+joinSource.width-150,joinSource.y+6);layout();
    require(dock.open("b") && dock.open("c") && dock.layout().children[0].children.size()==2,
        "moving back into the source switches the gesture to a split");
    dock.setLayout(stackedLayout);layout();
    // Junction: b's bottom-left corner meets c's top-left. Dragging into c splits c.
    doc.pointerMove(upper.x+3,upper.y+upper.height-3);doc.pointerDown(upper.x+3,upper.y+upper.height-3);
    doc.pointerMove(upper.x+5,upper.y+upper.height+20);doc.pointerMove(upper.x+6,lower.y+lower.height*.6f);
    require(dock.dragging(),"junction drag previews a split of the entered area");
    doc.pointerUp(upper.x+6,lower.y+lower.height*.6f);layout();
    require(dock.open("b") && dock.open("c") && dock.layout().children[1].children[1].children.size()==2 &&
        dock.layout().children[1].children[1].axis==Direction::Column,
        "dragging from a shared corner into an area splits that area");
    dock.setLayout(stackedLayout);layout();
    doc.pointerMove(joinSource.x+joinSource.width-3,joinSource.y+3);
    doc.pointerDown(joinSource.x+joinSource.width-3,joinSource.y+3,true);
    doc.pointerMove(lower.x+60,lower.y+lower.height*.5f);
    doc.pointerUp(lower.x+60,lower.y+lower.height*.5f);layout();
    require(dock.layout().children[0].tabs==std::vector<std::string>{"c"} &&
        dock.layout().children[1].children[1].tabs==std::vector<std::string>{"a"},
        "Ctrl+corner drag swaps two areas' contents");
    near(dock.panelBounds("a").y,lower.y,"swapped editor moves to the target area");
    int copies=0;
    dock.setDuplicable("c",[&](Node& page,const std::string& id){++copies;page.label(id);});
    dock.setLayout(initial);dock.show("c");layout();
    stacked=dock.panelBounds("c");
    doc.pointerMove(stacked.x+3,stacked.y+stacked.height-3);doc.pointerDown(stacked.x+3,stacked.y+stacked.height-3);
    doc.pointerMove(stacked.x+4,stacked.y+stacked.height-20);doc.pointerMove(stacked.x+4,stacked.y+stacked.height-150);
    doc.pointerUp(stacked.x+4,stacked.y+stacked.height-150);layout();
    require(copies==1 && dock.visible("c#2") && dock.visible("c"),"splitting a duplicable editor opens a copy like Blender");
    require(DockSpace::editorType("c#2")=="c","copies report their editor type");
    dock.setLayout(initial);layout();
    require(!dock.open("c#2") && dock.selectEditor("a","c") && dock.visible("c#2") && dock.open("b") && copies==1,
        "choosing an editor open elsewhere reuses a closed copy instead of swapping");
    auto saved=dock.layout();saved.children[0].tabs={"c#7"};saved.children[0].active="c#7";
    require(dock.setLayout(saved) && dock.visible("c#7") && copies==2,"saved layouts recreate editor copies");
    auto unknown=saved;unknown.children[0].tabs={"a#3"};unknown.children[0].active="a#3";
    require(!dock.setLayout(unknown),"editors that cannot be duplicated reject copy IDs");
    dock.setLayout(initial);layout();
    splitSource=dock.panelBounds("a");
    doc.pointerMove(splitSource.x+splitSource.width-4,splitSource.y+4);
    doc.pointerDown(splitSource.x+splitSource.width-4,splitSource.y+4);
    doc.pointerMove(splitSource.x+splitSource.width-30,splitSource.y+6);
    doc.pointerMove(splitSource.x+splitSource.width-170,splitSource.y+6);
    doc.pointerUp(splitSource.x+splitSource.width-170,splitSource.y+6);layout();
    require(dock.layout().children[0].children.size()==2 &&
        dock.layout().children[0].children[1].tabs.front().starts_with("__split_area_"),
        "editors that cannot be duplicated split into an empty area");
    dock.setLayout(initial);layout();
    auto panel=dock.panelBounds("a");doc.pointerDown(panel.x+panel.width-18,panel.y+12);doc.pointerUp(panel.x+panel.width-18,panel.y+12);
    require(!dock.open("a") && dock.layout().children.empty(),"close button collapses empty split without losing other pages");
    dock.close("b");dock.close("c");layout();require(dock.layout().tabs.empty(),"all panels can be closed");
    dock.show("a");layout();require(dock.visible("a") && field.text()=="Preserved inspector value","closed panel can be restored with original controls");
    require(changes>0,"workspace changes notify host for persistence");
    Document foreign;bool refused=false;try{a.reparent(foreign.root());}catch(const std::invalid_argument&){refused=true;}
    require(refused,"reparent rejects cross-document ownership");
    refused=false;try{a.reparent(field);}catch(const std::invalid_argument&){refused=true;}
    require(refused,"reparent rejects a non-container destination");
    auto& nested=a.column();refused=false;try{a.reparent(nested);}catch(const std::invalid_argument&){refused=true;}
    require(refused,"reparent rejects ownership cycles");
}
}
void bitmapImages() {
    Document doc;Style root;root.background={20,30,40,255};doc.root().setStyle(root);
    auto bitmap=std::make_shared<Surface>();bitmap->width=2;bitmap->height=1;bitmap->pixels={0xffff0000,0xff00ff00};
    Style style;style.width=100;style.height=100;style.padding=10;
    int clicks=0;auto& image=doc.root().button("",[&]{++clicks;}).setStyle(style).setImage(bitmap);
    const auto& rendered=doc.render(100,100);
    require(rendered.pixels[50*100+25]==0xffff0000 && rendered.pixels[50*100+75]==0xff00ff00,"bitmap widget renders source channels in correct order");
    require(rendered.pixels[15*100+50]==0xff141e28,"bitmap aspect fit leaves letterboxing inside padding");
    doc.pointerDown(25,50);doc.pointerUp(25,50);require(clicks==1,"image button retains normal input callbacks");
    image.setImage(nullptr);const auto& empty=doc.render(100,100);require(empty.pixels[50*100+25]!=0xffff0000,"clearing bitmap restores ordinary widget rendering");
    bool rejected=false;try {auto bad=std::make_shared<Surface>();bad->width=4;bad->height=4;image.setImage(bad);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"bitmap widget rejects incomplete pixel buffers");
}
void gpuDrawList() {
    Document doc;
    Style panel;panel.width=150;panel.height=70;panel.background={30,40,50,255};
    panel.radius=6;panel.borderWidth=2;panel.borderColor={80,90,100,255};
    auto& container=doc.root().column().setStyle(panel);
    Style label;label.width=100;label.height=30;label.textColor={240,220,200,255};
    auto& text=container.label("GPU label").setStyle(label);
    const auto& list=doc.renderGpu(300,100);
    require(list.width==300 && list.height==100 && list.commands.size()>=2,"GPU UI emits ordered geometry and text");
    require(list.commands[0].type==GpuDrawCommand::Type::RoundedRect &&
        list.commands[0].fill.r==30 && list.commands[0].radius==6,"GPU UI retains panel style");
    require(list.commands[1].type==GpuDrawCommand::Type::AtlasImage &&
        list.commands[1].bounds.x==text.bounds().x,"GPU UI atlas follows widget geometry");
    require(std::any_of(list.atlasPixels.begin(),list.atlasPixels.end(),[](uint32_t pixel){return pixel>>24;}),
        "GPU UI atlas contains rendered text");
    const auto revision=list.revision;
    const auto atlasRevision=list.atlasRevision;
    const float originalTextX=list.commands[1].bounds.x;
    require(doc.renderGpu(300,100).revision==revision,"clean GPU UI reuses atlas");
    require(list.rasterizedTiles==1,"initial GPU UI rasterizes the text tile");
    auto resized=container.style();resized.width=180;resized.margin.left=20;container.setStyle(resized);
    const auto& moved=doc.renderGpu(300,100);
    require(moved.rasterizedTiles==0,"moving a splitter reuses unchanged text pixels");
    require(moved.atlasRevision==atlasRevision,"moving a splitter reuses the GPU atlas upload");
    require(moved.commands[1].bounds.x==text.bounds().x && text.bounds().x>originalTextX,
        "cached text follows the new layout");
    text.setText("Changed");
    require(doc.renderGpu(300,100).revision==revision+2,"changed GPU UI rebuilds atlas");
    require(doc.renderGpu(300,100).rasterizedTiles==1,"changed text refreshes its tile");
    require(doc.renderGpu(300,100).atlasRevision==atlasRevision+1,"changed text updates the GPU atlas");
    const auto& software=doc.render(300,100);
    require(software.width==300 && software.pixels[5*300+25]!=0,
        "software UI remains available after GPU rendering");
    require(doc.renderGpu(300,100).revision==revision+3,
        "GPU atlas rebuilds after software fallback");
}
int main(int argc, char** argv) {
    try {
        if(argc>1 && std::string_view(argv[1])=="--profile-splitter") {
            const auto sourceProject=std::filesystem::path(GENESIS_ROOT)/"examples/editor_project";
            const auto project=std::filesystem::path(GENESIS_ROOT)/"examples/.splitter-profile-project";
            std::filesystem::create_directories(project);
            std::filesystem::copy_file(sourceProject/"main.gscene",project/"main.gscene",
                std::filesystem::copy_options::overwrite_existing);
            genesis::SceneDocument scene;std::string error;
            require(scene.load(project/"main.gscene",error),error.c_str());
            genesis::gameplay::GameplayWorld world;
            genesis::editor::GameEditor editor(world,scene,std::filesystem::path(GENESIS_ROOT)/"assets",project);
            editor.setAssetWatch(false);editor.resetLayout();editor.layout(1440,900);
            auto& document=editor.document();document.renderGpu(1440,900);
            const auto pane=editor.dockSpace().panelBounds("hierarchy");
            const auto inspectorBefore=editor.dockSpace().panelBounds("inspector").width;
            const float y=pane.y+pane.height*.5f;
            float start=-1;
            for(int x=900;x<1440;++x) {
                document.pointerMove(float(x),y);
                if(document.cursor()==Cursor::ResizeHorizontal){start=float(x);break;}
            }
            require(document.cursor()==Cursor::ResizeHorizontal,"profile pointer reaches the dock splitter");
            document.pointerDown(start,y);
            require(document.hasPointerCapture(),"profile captures the dock splitter");
            double inputMs=0,layoutMs=0,renderMs=0;
            uint64_t rasterized=0,uploads=0,atlasBytes=0;
            auto atlasRevision=document.renderGpu(1440,900).atlasRevision;
            for(int i=0;i<80;++i) {
                const float x=start-float(i%40);
                auto tick=std::chrono::steady_clock::now();document.pointerMove(x,y);
                auto inputEnd=std::chrono::steady_clock::now();editor.layout(1440,900);
                auto layoutEnd=std::chrono::steady_clock::now();
                const auto& list=document.renderGpu(1440,900);
                auto renderEnd=std::chrono::steady_clock::now();
                inputMs+=std::chrono::duration<double,std::milli>(inputEnd-tick).count();
                layoutMs+=std::chrono::duration<double,std::milli>(layoutEnd-inputEnd).count();
                renderMs+=std::chrono::duration<double,std::milli>(renderEnd-layoutEnd).count();
                rasterized+=list.rasterizedTiles;uploads+=list.atlasRevision!=atlasRevision;
                atlasRevision=list.atlasRevision;atlasBytes+=list.atlasPixels.size()*sizeof(uint32_t);
            }
            document.pointerUp(start-39,y);
            std::cout<<"splitter_x="<<start<<" inspector_after="
                <<editor.dockSpace().panelBounds("inspector").width<<'\n';
            require(std::abs(editor.dockSpace().panelBounds("inspector").width-inspectorBefore)>20,
                "profile actually resized the dock pane");
            std::cout<<"SPLITTER_PROFILE input_ms="<<inputMs/80<<" layout_ms="<<layoutMs/80
                <<" ui_ms="<<renderMs/80<<" rasterized="<<rasterized<<" atlas_updates="<<uploads
                <<" atlas_MB_per_frame="<<double(atlasBytes)/80/1048576<<'\n';
            return 0;
        }
        const std::filesystem::path output = argc > 1 ? argv[1] : "";
        layoutAndPaint(); input(); textAndClipping(); views(output); editorLayout(output); editorControls(output); draggableNumberField(); editingAndNavigation(); menusAndIcons(output); dropdownWidget(); popupTooltips(); resourceAndColor(output); docking(output);bitmapImages();gpuDrawList();
        std::cout << "PASS: " << checks << " UI checks (Yoga layout, ThorVG pixels, input, views)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
