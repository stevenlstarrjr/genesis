#include "ui/EditorWidgets.h"
#include "ui/Theme.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <unordered_set>

namespace genesis::ui {
namespace {
Style smallButton(Node&) {
    auto style = theme::button(); style.height = 20;
    style.padding = Insets(4,1); style.radius=0; style.background = {}; return style;
}
TreeItem* find(std::vector<TreeItem>& items, const std::string& id) {
    for (auto& item : items) {
        if (item.id == id) return &item;
        if (auto* child = find(item.children,id)) return child;
    }
    return nullptr;
}
void validate(const std::vector<TreeItem>& items, std::unordered_set<std::string>& ids) {
    for (const auto& item : items) {
        if (item.id.empty() || !ids.insert(item.id).second) throw std::invalid_argument("UI: tree IDs must be unique and nonempty");
        validate(item.children,ids);
    }
}
}
ScrollView::ScrollView(Node& parent) {
    Style viewport; viewport.grow = 1; viewport.scroll = true;
    m_viewport = &parent.column().setStyle(viewport);
    Style content; content.shrink = 0; content.gap = 0; content.padding.right = 9;
    m_content = &m_viewport->column().setStyle(content);
}
TreeView::TreeView(Node& parent, std::function<void(const std::string&)> selected)
    : m_scroll(parent), m_changed(std::move(selected)) {}
void TreeView::setItems(std::vector<TreeItem> items) {
    std::unordered_set<std::string> ids; validate(items,ids);
    const auto preserve = [&](auto&& self, std::vector<TreeItem>& list) -> void {
        for (auto& item : list) {
            if (const auto* previous = find(m_items,item.id)) item.expanded = previous->expanded;
            self(self,item.children);
        }
    };
    preserve(preserve,items);
    m_items = std::move(items);
    if (!ids.contains(m_selected)) m_selected.clear();
    rebuild();
}
void TreeView::select(const std::string& id) {
    if (!id.empty() && !find(m_items,id)) throw std::invalid_argument("UI: unknown tree item");
    m_selected = id;
    for (auto& row : m_rows) {
        auto style = row.button->style();
        style.background = row.id == id ? theme::selection : Color{};
        style.textColor = theme::text;
        row.button->setStyle(style);
    }
}
void TreeView::setExpanded(const std::string& id, bool expanded) {
    auto* item = find(m_items,id);
    if (!item) throw std::invalid_argument("UI: unknown tree item");
    item->expanded = expanded; rebuild();
}
void TreeView::rebuild() {
    std::string focused;
    for (const auto& row : m_rows)
        if (row.button->focused() || (row.toggle && row.toggle->focused())) focused = row.id;
    m_rows.clear(); m_scroll.content().clear();
    for (auto& item : m_items) append(item,0);
    select(m_selected);
    for (const auto& row : m_rows) if (row.id == focused) { row.button->focus(); break; }
}
void TreeView::focusSelected() {
    if (m_rows.empty()) return;
    auto row = std::find_if(m_rows.begin(),m_rows.end(),[&](const Row& r){ return r.id == m_selected; });
    (row == m_rows.end() ? m_rows.front() : *row).button->focus();
}
void TreeView::choose(const std::string& id) {
    const auto selected = id; // May refer to a row destroyed by the callback.
    select(selected); focusSelected();
    auto callback = m_changed; if (callback) callback(selected);
}
bool TreeView::navigate(const std::string& id, Key key) {
    auto row = std::find_if(m_rows.begin(),m_rows.end(),[&](const Row& r){ return r.id == id; });
    if (row == m_rows.end()) return false;
    const size_t index = size_t(row-m_rows.begin());
    auto* item = find(m_items,id);
    if (key == Key::Up) choose(m_rows[index ? index-1 : 0].id);
    else if (key == Key::Down) choose(m_rows[std::min(index+1,m_rows.size()-1)].id);
    else if (key == Key::Home) choose(m_rows.front().id);
    else if (key == Key::End) choose(m_rows.back().id);
    else if (key == Key::Right) {
        if (!item->children.empty()) {
            if (!item->expanded) setExpanded(id,true);
            else choose(item->children.front().id);
        }
    } else if (key == Key::Left) {
        if (!item->children.empty() && item->expanded) setExpanded(id,false);
        else if (!row->parent.empty()) choose(row->parent);
    } else return false;
    return true;
}
void TreeView::append(TreeItem& item, int depth, const std::string& parent) {
    auto& row = m_scroll.content().row(); auto style = row.style();
    style.height = 20; style.shrink = 0; style.padding.left = float(depth*14); style.gap = 0;
    row.setStyle(style);
    Node* disclosure = nullptr;
    if (!item.children.empty()) {
        const std::string id = item.id;
        auto& toggle = row.button("",[this,id] {
            auto* item = find(m_items,id); if (item) setExpanded(id,!item->expanded);
        });
        style = smallButton(toggle); style.width = 16; style.shrink = 0; style.padding = 0; toggle.setStyle(style);
        toggle.setIcon(item.expanded ? Icon::ChevronDown : Icon::ChevronRight,10);
        toggle.onKeyDown([this,id](Key key){ return navigate(id,key); }); disclosure = &toggle;
    } else {
        Style spacer; spacer.width = 16; spacer.shrink = 0; row.column().setStyle(spacer);
    }
    const std::string id = item.id;
    auto& button = row.button(item.text,[this,id] { choose(id); });
    button.onKeyDown([this,id](Key key){ return navigate(id,key); });
    style = smallButton(button); style.grow = 1; style.textAlign = 0; button.setStyle(style);
    button.setIcon(item.icon,13);
    m_rows.push_back({item.id,parent,&button,disclosure});
    if (item.expanded) for (auto& child : item.children) append(child,depth+1,item.id);
}
Tabs::Tabs(Node& parent) {
    Style root; root.grow = 1; m_root = &parent.column().setStyle(root);
    m_header = &m_root->row(); auto bar = m_header->style(); bar.height = 23; bar.shrink = 0; bar.background=theme::input;
    m_header->setStyle(bar);
    Style body; body.grow = 1; m_body = &m_root->column().setStyle(body);
}
Node& Tabs::add(std::string title) {
    const auto index = m_pages.size();
    auto& button = m_header->button(std::move(title),[this,index] { activate(index); });
    auto style = smallButton(button); style.height=23; style.padding = Insets(10,3); button.setStyle(style);
    Style content; content.grow = 1;
    auto& page = m_body->column().setStyle(content);
    m_pages.push_back({&button,&page}); activate(m_active); return page;
}
void Tabs::activate(size_t index) {
    if (index >= m_pages.size()) throw std::out_of_range("UI: unknown tab");
    m_active = index;
    for (size_t i = 0; i < m_pages.size(); ++i) {
        m_pages[i].content->setVisible(i == index);
        auto style = m_pages[i].button->style();
        style.background = i == index ? theme::panel : theme::input;
        style.textColor = i == index ? theme::text : theme::muted;
        m_pages[i].button->setStyle(style);
    }
}
void Tabs::setIcon(size_t index,Icon icon) { m_pages.at(index).button->setIcon(icon,12); }
MenuBar::MenuBar(Document& document,Node& bar) : m_document(document),m_bar(&bar) {
    m_popup=&document.root().column();
    Style popup; popup.absolute=true; popup.width=246; popup.padding=3;
    popup.background={48,48,48,255}; popup.borderWidth=1; popup.borderColor=theme::border;
    m_popup->setStyle(popup).setVisible(false);
}
Node& MenuBar::add(std::string title,std::vector<MenuItem> items) {
    const size_t index=m_menus.size();
    auto& button=m_bar->button(std::move(title),[this,index]{open(index);});
    auto style=theme::button(); style.height=24; style.radius=0; style.background={};
    style.padding=Insets(8,3); button.setStyle(style);
    button.onKeyDown([this,index](Key key){if(key!=Key::Down) return false;open(index);return true;});
    m_menus.push_back({&button,std::move(items)});
    return button;
}
void MenuBar::open(size_t index) {
    m_document.closePopup(); m_popup->clear(); m_commands.clear();
    const auto anchor=m_menus[index].button->bounds();
    auto style=m_popup->style();
    style.left=std::clamp(anchor.x,0.0f,std::max(0.0f,m_document.root().bounds().width-246));
    style.top=anchor.y+anchor.height; style.maxHeight=std::max(30.0f,m_document.root().bounds().height-style.top-4); style.scroll=true;
    m_popup->setStyle(style);
    for(const auto& item:m_menus[index].items) {
        if(item.text.empty()) {
            Style separator; separator.height=1; separator.shrink=0; separator.margin=Insets(4,3); separator.background=theme::border;
            m_popup->column().setStyle(separator); continue;
        }
        const auto action=item.action;
        std::string caption=item.text;
        if(item.checked)caption=std::string(item.checked()?"✓ ":"   ")+caption;
        if(!item.shortcut.empty()) caption+=std::string(std::max(2,30-int(item.text.size()+item.shortcut.size())),' ')+item.shortcut;
        auto& command=m_popup->button(caption,[this,action]{m_document.closePopup();if(action) action();});
        auto commandStyle=theme::button(); commandStyle.height=23; commandStyle.textAlign=0; commandStyle.radius=0;
        commandStyle.background={}; commandStyle.hoverBackground=theme::selection; command.setStyle(commandStyle);
        const bool enabled=!item.enabled || item.enabled(); command.setEnabled(enabled);
        if(enabled) {
            const size_t at=m_commands.size(); m_commands.push_back(&command);
            command.onKeyDown([this,index,at](Key key){
                if(key==Key::Left || key==Key::Right) { open((index+m_menus.size()+(key==Key::Left?-1:1))%m_menus.size());return true; }
                if(key!=Key::Up && key!=Key::Down && key!=Key::Home && key!=Key::End) return false;
                size_t next=key==Key::Home ? 0 : key==Key::End ? m_commands.size()-1 : (at+m_commands.size()+(key==Key::Up?-1:1))%m_commands.size();
                m_commands[next]->focus();return true;
            });
        }
    }
    m_menus[index].button->focus();
    m_document.openPopup(*m_popup,*m_bar);
}
Dropdown::Dropdown(Document& document,Node& parent,std::vector<DropdownChoice> choices,
    size_t selected,std::function<void(size_t)> changed)
    :m_document(document),m_choices(std::move(choices)),m_changed(std::move(changed)) {
    if(m_choices.empty() || selected>=m_choices.size())throw std::invalid_argument("UI: invalid dropdown selection");
    m_button=&parent.button("",[this]{open();});
    auto buttonStyle=theme::button();buttonStyle.height=23;buttonStyle.minHeight=0;
    buttonStyle.fontSize=9;buttonStyle.padding=Insets(5,2);buttonStyle.radius=3;
    buttonStyle.background={48,48,48,255};buttonStyle.hoverBackground={65,65,65,255};
    buttonStyle.borderWidth=0;m_button->setStyle(buttonStyle);
    m_button->onKeyDown([this](Key key){if(key!=Key::Down && key!=Key::Space)return false;open();return true;});
    m_popup=&document.root().column();
    Style popup;popup.absolute=true;popup.width=170;popup.padding=3;
    popup.background={48,48,48,255};popup.borderWidth=1;popup.borderColor=theme::border;
    popup.radius=3;m_popup->setStyle(popup).setVisible(false);
    select(selected);
}
void Dropdown::select(size_t index,bool emit) {
    if(index>=m_choices.size() || !m_choices[index].enabled)throw std::out_of_range("UI: invalid dropdown choice");
    const bool changed=index!=m_selected;
    m_selected=index;
    m_button->setText(m_buttonLabel.empty()?m_choices[index].label+"  v":m_buttonLabel);
    if(m_choices[index].blenderIcon.empty())m_button->setIcon(Icon::None);
    else m_button->setBlenderIcon(m_choices[index].blenderIcon,14);
    if(emit && changed && m_changed)m_changed(index);
}
void Dropdown::setButtonLabel(std::string label) {
    m_buttonLabel=std::move(label);
    m_button->setText(m_buttonLabel.empty()?m_choices[m_selected].label+"  v":m_buttonLabel);
}
void Dropdown::open() {
    m_document.closePopup();m_popup->clear();m_items.clear();
    const auto anchor=m_button->bounds(),root=m_document.root().bounds();
    auto style=m_popup->style();
    style.width=std::max(170.0f,anchor.width);
    style.left=std::clamp(anchor.x,0.0f,std::max(0.0f,root.width-style.width.value));
    style.top=anchor.y+anchor.height;
    style.maxHeight=std::max(30.0f,root.height-style.top-4);style.scroll=true;
    m_popup->setStyle(style);
    for(size_t index=0;index<m_choices.size();++index) {
        const auto& choice=m_choices[index];
        auto& item=m_popup->button(choice.label,[this,index]{m_document.closePopup();select(index,true);});
        auto itemStyle=theme::button();itemStyle.height=24;itemStyle.minHeight=0;
        itemStyle.textAlign=0;itemStyle.radius=0;itemStyle.padding=Insets(6,2);
        itemStyle.background=index==m_selected?theme::selection:Color{};
        itemStyle.hoverBackground=theme::selection;
        item.setStyle(itemStyle).setEnabled(choice.enabled);
        if(!choice.blenderIcon.empty())item.setBlenderIcon(choice.blenderIcon,14);
        m_items.push_back(&item);
        item.onKeyDown([this,index](Key key){
            if(key!=Key::Up && key!=Key::Down)return false;
            for(size_t offset=1;offset<=m_choices.size();++offset) {
                const size_t next=key==Key::Up
                    ?(index+m_choices.size()-offset%m_choices.size())%m_choices.size()
                    :(index+offset)%m_choices.size();
                if(m_choices[next].enabled){m_items[next]->focus();break;}
            }
            return true;
        });
    }
    m_document.openPopup(*m_popup,*m_button);
}
ResourcePicker::ResourcePicker(Document& document):m_document(document) {
    m_popup=&document.root().column();Style popup;popup.absolute=true;popup.width=380;popup.height=330;
    popup.padding=8;popup.gap=6;popup.background=theme::panel;popup.borderWidth=1;popup.borderColor=theme::border;m_popup->setStyle(popup).setVisible(false);
    m_title=&m_popup->label("").setStyle(theme::label(10));
    m_search=&m_popup->textField("",[this](const std::string&){refresh();}).setStyle(theme::field()).setPlaceholder("Search");
    m_search->onKeyDown([this](Key key){
        if(key==Key::Down && !m_buttons.empty()){m_buttons.front()->focus();return true;}
        if(key==Key::Enter && !m_ids.empty()){choose(m_ids.front());return true;}return false;
    });
    m_results=std::make_unique<ScrollView>(*m_popup);
    m_pathRow=&m_popup->row();auto row=m_pathRow->style();row.gap=4;row.shrink=0;m_pathRow->setStyle(row);
    auto field=theme::field();field.grow=1;m_path=&m_pathRow->textField("",{}).setStyle(field).setPlaceholder("Full path to .glb / .gltf");
    m_path->onKeyDown([this](Key key){if(key!=Key::Enter)return false;if(!m_path->text().empty())choose(m_path->text());return true;});
    m_pathRow->button("Use Path",[this]{if(!m_path->text().empty())choose(m_path->text());}).setStyle(theme::button());
}
void ResourcePicker::open(Node& anchor,std::string title,std::vector<ResourceItem> items,std::function<void(const std::string&)> chosen,bool allowPath) {
    m_document.closePopup();m_items=std::move(items);m_chosen=std::move(chosen);
    m_title->setText(std::move(title));m_search->setText("");m_path->setText("");m_pathRow->setVisible(allowPath);refresh();
    const auto documentSize=m_document.root().bounds();m_document.layout(documentSize.width,documentSize.height);
    const auto bounds=anchor.bounds(),root=m_document.root().bounds();auto style=m_popup->style();
    style.width=std::min(380.0f,root.width-16);style.height=std::min(330.0f,root.height-16);
    style.left=std::clamp(bounds.x,8.0f,std::max(8.0f,root.width-style.width.value-8));
    style.top=std::clamp(bounds.y+bounds.height,8.0f,std::max(8.0f,root.height-style.height.value-8));m_popup->setStyle(style);
    anchor.focus();m_document.openPopup(*m_popup,anchor);m_search->focus();
}
void ResourcePicker::choose(std::string id) {
    auto callback=m_chosen;m_document.closePopup();if(callback)callback(id);
}
void ResourcePicker::refresh() {
    auto lower=[](std::string text){for(auto& c:text)c=char(std::tolower(static_cast<unsigned char>(c)));return text;};
    auto& content=m_results->content();content.clear();m_buttons.clear();m_ids.clear();const auto query=lower(m_search->text());
    for(const auto& item:m_items) {
        if(lower(item.name+" "+item.detail).find(query)==std::string::npos)continue;
        auto s=theme::button();s.textAlign=0;s.height=25;s.radius=0;s.background={};
        auto& button=content.button(item.name,[this,id=item.id]{choose(id);}).setStyle(s).setIcon(item.icon,13).setEnabled(item.enabled);
        if(!item.detail.empty()){s=theme::label(8,theme::muted);s.padding=Insets(8,1);s.textWrap=TextWrap::Ellipsis;content.label(item.detail).setStyle(s);}
        if(item.enabled) {
            const size_t at=m_buttons.size();m_buttons.push_back(&button);m_ids.push_back(item.id);
            button.onKeyDown([this,at](Key key){
                if(key!=Key::Up && key!=Key::Down && key!=Key::Home && key!=Key::End)return false;
                if(key==Key::Up && at==0){m_search->focus();return true;}
                const size_t next=key==Key::Home?0:key==Key::End?m_buttons.size()-1:(at+m_buttons.size()+(key==Key::Up?-1:1))%m_buttons.size();
                m_buttons[next]->focus();return true;
            });
        }
    }
    if(m_buttons.empty())content.label("No available matches").setStyle(theme::label(9,theme::muted));
    m_results->viewport().scrollTo(0);
}
namespace {
std::array<float,3> hsvColor(float hue,float saturation,float value) {
    const float h=std::fmod(hue+1.0f,1.0f)*6.0f;
    const float chroma=value*saturation;
    const float x=chroma*(1.0f-std::abs(std::fmod(h,2.0f)-1.0f));
    std::array<float,3> rgb{};
    if(h<1)rgb={chroma,x,0};else if(h<2)rgb={x,chroma,0};
    else if(h<3)rgb={0,chroma,x};else if(h<4)rgb={0,x,chroma};
    else if(h<5)rgb={x,0,chroma};else rgb={chroma,0,x};
    for(auto& channel:rgb)channel+=value-chroma;
    return rgb;
}
Color swatchColor(std::array<float,3> value) {
    return {uint8_t(std::clamp(value[0],0.0f,1.0f)*255),
            uint8_t(std::clamp(value[1],0.0f,1.0f)*255),
            uint8_t(std::clamp(value[2],0.0f,1.0f)*255),255};
}
std::string hexColor(std::array<float,3> value) {
    char hex[8];const auto color=swatchColor(value);
    std::snprintf(hex,sizeof(hex),"#%02X%02X%02X",color.r,color.g,color.b);
    return hex;
}
bool parseHexColor(const std::string& text,std::array<float,3>& color) {
    const auto digits=text.starts_with('#')?text.substr(1):text;
    if(digits.size()!=6)return false;
    unsigned int packed=0;
    const auto result=std::from_chars(digits.data(),digits.data()+digits.size(),packed,16);
    if(result.ec!=std::errc{} || result.ptr!=digits.data()+digits.size())return false;
    color={float((packed>>16)&255)/255,float((packed>>8)&255)/255,float(packed&255)/255};
    return true;
}
}
ColorPicker::ColorPicker(Document& document):m_document(document) {
    m_popup=&document.root().column();
    Style popup;popup.absolute=true;popup.width=294;popup.padding=10;popup.gap=7;
    popup.background=theme::panel;popup.borderWidth=1;popup.borderColor=theme::border;
    m_popup->setStyle(popup).setVisible(false);
    m_popup->label("Color picker").setStyle(theme::label(10));
    auto& canvas=m_popup->row();auto row=canvas.style();row.gap=8;row.shrink=0;canvas.setStyle(row);
    auto gradient=theme::button();gradient.width=220;gradient.height=220;gradient.padding=0;gradient.radius=0;gradient.background={};
    m_square=&canvas.button("",[]{}).setStyle(gradient);
    m_square->onPointerDown([this](float x,float y){pickSquare(x,y);});
    m_square->onDrag([this](float x,float y){pickSquare(x,y);},[](bool){});
    m_square->onKeyDown([this](Key key){
        const float step=1.0f/219;
        if(key==Key::Left)m_saturation=std::max(0.0f,m_saturation-step);
        else if(key==Key::Right)m_saturation=std::min(1.0f,m_saturation+step);
        else if(key==Key::Up)m_brightness=std::min(1.0f,m_brightness+step);
        else if(key==Key::Down)m_brightness=std::max(0.0f,m_brightness-step);
        else return false;
        setColor(hsvColor(m_hue,m_saturation,m_brightness),true);return true;
    });
    gradient.width=24;m_hueStrip=&canvas.button("",[]{}).setStyle(gradient);
    m_hueStrip->onPointerDown([this](float x,float y){pickHue(x,y);});
    m_hueStrip->onDrag([this](float x,float y){pickHue(x,y);},[](bool){});
    m_hueStrip->onKeyDown([this](Key key){
        if(key!=Key::Up && key!=Key::Down)return false;
        m_hue=std::clamp(m_hue+(key==Key::Down?1.0f:-1.0f)/219,0.0f,1.0f);
        setColor(hsvColor(m_hue,m_saturation,m_brightness),true);return true;
    });
    auto& previewRow=m_popup->row();row=previewRow.style();row.gap=7;row.shrink=0;row.align=Align::Center;previewRow.setStyle(row);
    auto preview=theme::label();preview.width=38;preview.height=22;preview.borderWidth=1;preview.borderColor=theme::border;
    m_preview=&previewRow.label("").setStyle(preview);
    auto hexStyle=theme::field();hexStyle.grow=1;
    m_hex=&previewRow.textField("",{}).setStyle(hexStyle).setPlaceholder("#RRGGBB");
    m_hex->onTextCommitted([this](const std::string& text){
        std::array<float,3> color{};
        if(parseHexColor(text,color))setColor(color,true);
        else refresh();
    });
    auto& values=m_popup->row();row=values.style();row.gap=5;row.shrink=0;row.align=Align::Center;values.setStyle(row);
    const char* names[]{"H","S","B"};
    for(int i=0;i<3;++i) {
        values.label(names[i]).setStyle(theme::label(8,theme::muted));
        auto field=theme::field();field.width=0;field.grow=1;field.padding=Insets(3,2);
        m_hsvFields[i]=&values.numberField(i==2?100:0,0,i==0?360:100,1,[this,i](float value){
            if(i==0)m_hue=value/360;else if(i==1)m_saturation=value/100;else m_brightness=value/100;
            setColor(hsvColor(m_hue,m_saturation,m_brightness),true);
        }).setStyle(field);
    }
}
void ColorPicker::open(Node& anchor,std::array<float,3> value,std::function<void(std::array<float,3>)> changed) {
    m_changed=std::move(changed);setColor(value,false);
    const auto root=m_document.root().bounds(),bounds=anchor.bounds();auto s=m_popup->style();
    s.left=std::clamp(bounds.x,4.0f,std::max(4.0f,root.width-298));
    s.top=std::clamp(bounds.y+bounds.height,4.0f,std::max(4.0f,root.height-330));
    m_popup->setStyle(s);anchor.focus();m_document.openPopup(*m_popup,anchor);
}
void ColorPicker::setColor(std::array<float,3> value,bool emit) {
    for(auto& channel:value)channel=std::clamp(channel,0.0f,1.0f);
    m_value=value;
    const float maximum=std::max({value[0],value[1],value[2]});
    const float minimum=std::min({value[0],value[1],value[2]});
    const float delta=maximum-minimum;
    m_brightness=maximum;m_saturation=maximum>0?delta/maximum:0;
    if(delta>0.0001f) {
        float hue=0;
        if(maximum==value[0])hue=(value[1]-value[2])/delta;
        else if(maximum==value[1])hue=2+(value[2]-value[0])/delta;
        else hue=4+(value[0]-value[1])/delta;
        m_hue=std::fmod(hue/6+1,1.0f);
    }
    refresh();if(emit && m_changed)m_changed(m_value);
}
void ColorPicker::pickSquare(float x,float y) {
    const auto bounds=m_square->bounds();
    m_saturation=std::clamp((x-bounds.x)/219.0f,0.0f,1.0f);
    m_brightness=1.0f-std::clamp((y-bounds.y)/219.0f,0.0f,1.0f);
    setColor(hsvColor(m_hue,m_saturation,m_brightness),true);
}
void ColorPicker::pickHue(float,float y) {
    const auto bounds=m_hueStrip->bounds();
    m_hue=std::clamp((y-bounds.y)/219.0f,0.0f,1.0f);
    setColor(hsvColor(m_hue,m_saturation,m_brightness),true);
}
void ColorPicker::refresh() {
    auto preview=m_preview->style();preview.background=swatchColor(m_value);m_preview->setStyle(preview);
    m_hex->setText(hexColor(m_value));
    m_hsvFields[0]->setValue(std::round(m_hue*360));
    m_hsvFields[1]->setValue(std::round(m_saturation*100));
    m_hsvFields[2]->setValue(std::round(m_brightness*100));
    auto square=std::make_shared<Surface>();square->width=square->height=220;square->pixels.resize(220*220);
    const int markerX=int(std::round(m_saturation*219)),markerY=int(std::round((1-m_brightness)*219));
    for(int y=0;y<220;++y)for(int x=0;x<220;++x) {
        auto c=swatchColor(hsvColor(m_hue,float(x)/219,1.0f-float(y)/219));
        const int dx=x-markerX,dy=y-markerY,distance=dx*dx+dy*dy;
        if(distance>=16 && distance<=36)c={0,0,0,255};
        if(distance>=9 && distance<16)c={255,255,255,255};
        square->pixels[y*220+x]=0xff000000u|(uint32_t(c.r)<<16)|(uint32_t(c.g)<<8)|c.b;
    }
    m_square->setImage(square);
    auto hue=std::make_shared<Surface>();hue->width=24;hue->height=220;hue->pixels.resize(24*220);
    const int hueY=int(std::round(m_hue*219));
    for(int y=0;y<220;++y)for(int x=0;x<24;++x) {
        auto c=swatchColor(hsvColor(float(y)/219,1,1));
        if(std::abs(y-hueY)<=1)c={x<12?uint8_t(255):uint8_t(0),x<12?uint8_t(255):uint8_t(0),x<12?uint8_t(255):uint8_t(0),255};
        hue->pixels[y*24+x]=0xff000000u|(uint32_t(c.r)<<16)|(uint32_t(c.g)<<8)|c.b;
    }
    m_hueStrip->setImage(hue);
}
ColorField::ColorField(Node& parent,std::function<void(std::array<float,3>)> changed):m_changed(std::move(changed)) {
    auto& hexRow=parent.row();auto s=hexRow.style();s.gap=4;s.shrink=0;s.align=Align::Center;hexRow.setStyle(s);
    s=theme::field();s.grow=1;
    m_hex=&hexRow.textField("",{}).setStyle(s).setPlaceholder("#RRGGBB");
    m_hex->onTextCommitted([this](const std::string& text){
        std::array<float,3> color{};
        if(parseHexColor(text,color)){setValue(color);if(m_changed)m_changed(color);}
        else m_hex->setText(hexColor(m_value));
    });
    s=theme::button();s.width=42;
    m_pick=&hexRow.button("Pick",[this]{
        if(m_picker)m_picker->open(*m_pick,m_value,[this](auto color){setValue(color);if(m_changed)m_changed(color);});
    }).setStyle(s);
    auto& row=parent.row();s=row.style();s.gap=3;s.shrink=0;s.align=Align::Center;row.setStyle(s);
    const char* names[]{"R","G","B"};
    for(int i=0;i<3;++i) {
        row.label(names[i]).setStyle(theme::label(8,theme::muted));s=theme::field();s.width=0;s.grow=1;s.padding=Insets(3,2);
        m_fields[i]=&row.numberField(1,0,1,.05f,[this,i](float value){auto color=m_value;color[i]=value;setValue(color);if(m_changed)m_changed(color);}).setStyle(s);
    }
    setValue(m_value);
}
void ColorField::setValue(std::array<float,3> value) {
    m_value=value;for(int i=0;i<3;++i)m_fields[i]->setValue(value[i]);
    m_hex->setText(hexColor(value));
}
Node& section(Node& parent, std::string title, bool expanded) {
    auto& group = parent.column(); auto style = group.style(); style.shrink = 0; style.gap = 0; group.setStyle(style);
    auto& header = group.button(title,[]{});
    style = smallButton(header); style.height=23; style.background = theme::raised; style.textAlign = 0; header.setStyle(style);
    header.setIcon(expanded ? Icon::ChevronDown : Icon::ChevronRight,12);
    auto& content = group.column(); style = content.style(); style.gap = 3; style.padding=Insets(8,6); style.shrink = 0; content.setStyle(style).setVisible(expanded);
    header.onClick([&header,&content,title,expanded]() mutable {
        expanded = !expanded; content.setVisible(expanded); header.setIcon(expanded ? Icon::ChevronDown : Icon::ChevronRight,12);
    });
    return content;
}
Node& property(Node& parent, std::string caption) {
    auto& row = parent.column(); auto style = row.style(); style.gap = 4; style.shrink = 0; row.setStyle(style);
    auto label = theme::label(9,theme::muted);
    row.label(std::move(caption)).setStyle(label);
    return row;
}
}
