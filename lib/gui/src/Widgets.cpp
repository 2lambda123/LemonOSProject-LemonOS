#include <Lemon/GUI/Model.h>
#include <Lemon/GUI/Theme.h>
#include <Lemon/GUI/Widgets.h>
#include <Lemon/GUI/Window.h>
#include <Lemon/Graphics/Surface.h>

#include <Lemon/Core/Keyboard.h>
#include <Lemon/Core/Unicode.h>

#include <algorithm>
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <string>

namespace Lemon::GUI {
Widget::Widget() {}

Widget::Widget(rect_t bounds, LayoutSize newSizeX, LayoutSize newSizeY) {
    this->bounds = bounds;

    sizeX = newSizeX;
    sizeY = newSizeY;
}

Widget::~Widget() {}

void Widget::SetLayout(LayoutSize newSizeX, LayoutSize newSizeY, WidgetAlignment newAlign,
                       WidgetAlignment newAlignVert) {
    sizeX = newSizeX;
    sizeY = newSizeY;
    align = newAlign;
    verticalAlign = newAlignVert;
    UpdateFixedBounds();
};

void Widget::Paint(__attribute__((unused)) surface_t* surface) {}

void Widget::OnMouseEnter(vector2i_t mousePos) { OnMouseMove(mousePos); }

void Widget::OnMouseExit(__attribute__((unused)) vector2i_t mousePos) {}

void Widget::OnMouseDown(__attribute__((unused)) vector2i_t mousePos) {}

void Widget::OnMouseUp(__attribute__((unused)) vector2i_t mousePos) {}

void Widget::OnRightMouseDown(__attribute__((unused)) vector2i_t mousePos) {}

void Widget::OnRightMouseUp(__attribute__((unused)) vector2i_t mousePos) {}

void Widget::OnHover(__attribute__((unused)) vector2i_t mousePos) {}

void Widget::OnMouseMove(__attribute__((unused)) vector2i_t mousePos) {}

void Widget::OnDoubleClick(__attribute__((unused)) vector2i_t mousePos) {}

void Widget::OnKeyPress(__attribute__((unused)) int key) {}

void Widget::OnCommand(__attribute__((unused)) unsigned short key) {}

void Widget::OnActive() {}

void Widget::OnInactive() {}

void Widget::UpdateFixedBounds() {
    fixedBounds.pos = bounds.pos;

    if (parent) {
        fixedBounds.pos += parent->GetFixedBounds().pos;

        if (sizeX == LayoutSize::Stretch) {
            if (align == WidgetAlignment::WAlignRight)
                fixedBounds.width = (parent->GetFixedBounds().size.x - bounds.pos.x) - parent->GetFixedBounds().pos.x;
            else
                fixedBounds.width = parent->GetFixedBounds().width - bounds.width - bounds.x;
        } else {
            fixedBounds.width = bounds.width;
        }

        if (sizeY == LayoutSize::Stretch) {
            if (verticalAlign == WidgetAlignment::WAlignBottom)
                fixedBounds.height = (parent->GetFixedBounds().height - bounds.height) - parent->GetFixedBounds().y;
            else
                fixedBounds.height = parent->GetFixedBounds().height - bounds.height - bounds.y;
        } else {
            fixedBounds.height = bounds.height;
        }

        if (align == WidgetAlignment::WAlignRight) {
            fixedBounds.pos.x =
                (parent->GetFixedBounds().pos.x + parent->GetFixedBounds().size.x) - bounds.pos.x - fixedBounds.width;
        } else if (align == WAlignCentre) {
            fixedBounds.pos.x = parent->GetFixedBounds().width / 2 - fixedBounds.width / 2 + bounds.x;
        }

        if (verticalAlign == WidgetAlignment::WAlignBottom) {
            fixedBounds.pos.y =
                (parent->GetFixedBounds().y + parent->GetFixedBounds().height) - bounds.y - fixedBounds.height;
        } else if (verticalAlign == WAlignCentre) {
            fixedBounds.pos.y = parent->GetFixedBounds().height / 2 - fixedBounds.height / 2 + bounds.y;
        }
    } else {
        fixedBounds.size = bounds.size;
    }
}

//////////////////////////
// Container
//////////////////////////
Container::Container(rect_t bounds) : Widget(bounds) {}

Container::~Container() {
    for (auto& widget : children) {
        delete widget;
    }
}

void Container::SetWindow(Window* w) {
    window = w;

    for (auto* child : children) {
        child->SetWindow(w);
    }
}

void Container::AddWidget(Widget* w) {
    children.push_back(w);

    w->SetParent(this);
    w->SetWindow(window);

    if (!active) {
        active = w;
    }

    UpdateFixedBounds();
}

void Container::RemoveWidget(Widget* w) {
    w->SetParent(nullptr);
    w->SetWindow(nullptr);

    if (active == w)
        active = nullptr;
    if (lastMousedOver == w)
        lastMousedOver = nullptr;

    children.erase(std::remove(children.begin(), children.end(), w), children.end());
}

void Container::Paint(surface_t* surface) {
    if (background.a == 255) {
        Graphics::DrawRect(fixedBounds, background, surface);
    }

    for (Widget* w : children) {
        w->Paint(surface);
    }
}

void Container::OnMouseEnter(vector2i_t mousePos) {
    for (Widget* w : children) {
        if (Graphics::PointInRect(w->GetFixedBounds(), mousePos)) {
            w->OnMouseEnter(mousePos);

            lastMousedOver = w;
            break;
        }
    }
}

void Container::OnMouseExit(vector2i_t mousePos) {
    if (lastMousedOver) {
        lastMousedOver->OnMouseExit(mousePos);
    }

    lastMousedOver = nullptr;
}

void Container::OnMouseDown(vector2i_t mousePos) {
    for (Widget* w : children) {
        if (Graphics::PointInRect(w->GetFixedBounds(), mousePos)) {
            if (active != w) {
                if (active) {
                    active->OnInactive();
                }

                w->OnActive();
            }
            active = w;
            w->OnMouseDown(mousePos);
            break;
        }
    }
}

void Container::OnMouseUp(vector2i_t mousePos) {
    if (active) {
        active->OnMouseUp(mousePos);
    }
}

void Container::OnRightMouseDown(vector2i_t mousePos) {
    for (Widget* w : children) {
        if (Graphics::PointInRect(w->GetFixedBounds(), mousePos)) {
            active = w;
            w->OnRightMouseDown(mousePos);
            break;
        }
    }
}

void Container::OnRightMouseUp(vector2i_t mousePos) {
    if (active) {
        active->OnRightMouseUp(mousePos);
    }
}

void Container::OnMouseMove(vector2i_t mousePos) {
    for (Widget* w : children) {
        if (Graphics::PointInRect(w->GetFixedBounds(), mousePos)) {
            if (w == lastMousedOver) {
                break;
            } else {
                if (lastMousedOver) {
                    lastMousedOver->OnMouseExit(mousePos);
                }

                w->OnMouseEnter(mousePos);

                lastMousedOver = w;
            }
        } else if (w == lastMousedOver) {
            lastMousedOver = nullptr;
        }
    }

    if (active) {
        active->OnMouseMove(mousePos);
    }
}

void Container::OnDoubleClick(vector2i_t mousePos) {
    if (active &&
        Graphics::PointInRect(active->GetFixedBounds(),
                              mousePos)) { // If user hasnt clicked on same widget then this aint a double click
        active->OnDoubleClick(mousePos);
    } else {
        OnMouseDown(mousePos);
    }
}

void Container::OnKeyPress(int key) {
    if (active) {
        active->OnKeyPress(key);
    }
}

void Container::UpdateFixedBounds() {
    Widget::UpdateFixedBounds();

    for (Widget* w : children) {
        w->UpdateFixedBounds();
    }
}

//////////////////////////
// LayoutContainer
//////////////////////////
LayoutContainer::LayoutContainer(rect_t bounds, vector2i_t minItemSize) : Container(bounds) {
    this->itemSize = minItemSize;
}

void LayoutContainer::AddWidget(Widget* w) {
    Container::AddWidget(w);

    UpdateFixedBounds();
}

void LayoutContainer::RemoveWidget(Widget* w) {
    Container::RemoveWidget(w);

    UpdateFixedBounds();
}

void LayoutContainer::UpdateFixedBounds() {
    Widget::UpdateFixedBounds();

    if (!children.size()) {
        return;
    }

    vector2i_t pos = {xPadding, yPadding};
    isOverflowing = false;

    int itemWidth = itemSize.x;
    int itemHeight = itemSize.y;
    int itemsPerRow = (fixedBounds.width - xPadding) / (itemSize.x + xPadding);

    // Fill the width of the container if requested
    if (xFill && itemsPerRow >= (int)children.size()) {
        itemWidth = ((fixedBounds.width - xPadding) / children.size()) - xPadding;
    }

    for (Widget* w : children) {
        w->SetBounds({pos, {itemWidth, itemHeight}});
        w->SetLayout(LayoutSize::Fixed, LayoutSize::Fixed, WidgetAlignment::WAlignLeft);

        w->UpdateFixedBounds();

        pos.x += itemWidth + xPadding;

        if (pos.x + itemWidth > fixedBounds.width) {
            pos.x = xPadding;
            pos.y += itemHeight + yPadding;
        }
    }

    if (pos.y + itemSize.y > fixedBounds.height)
        isOverflowing = true;
}

//////////////////////////
// Button
//////////////////////////
Button::Button(const char* _label, rect_t _bounds) : Widget(_bounds) {
    label = _label;
    labelLength = Graphics::GetTextLength(label.c_str());
}

void Button::SetLabel(const char* _label) {
    label = _label;
    labelLength = Graphics::GetTextLength(label.c_str());
}

void Button::DrawButtonLabel(surface_t* surface) {
    rgba_colour_t colour;
    vector2i_t btnPos = fixedBounds.pos;

    colour = Theme::Current().ColourTextLight();

    if (labelAlignment == TextAlignment::Centre) {
        Graphics::DrawString(label.c_str(), btnPos.x + (fixedBounds.size.x / 2) - (labelLength / 2),
                             btnPos.y + (fixedBounds.size.y / 2 - 6 - 2), colour.r, colour.g, colour.b, surface);
    } else {
        Graphics::DrawString(label.c_str(), btnPos.x + 3, btnPos.y + (fixedBounds.size.y / 2 - 6 - 2), colour.r,
                             colour.g, colour.b, surface);
    }
}

void Button::Paint(surface_t* surface) {
    if (pressed) {
        Lemon::Graphics::DrawRoundedRect(fixedBounds, Theme::Current().ColourBorder(), 5, 5, 5, 5, surface);
    } else {
        Rect innerRect = {fixedBounds.pos + Vector2i{1, 1}, fixedBounds.size - Vector2i{2, 3}};
        ;
        Lemon::Graphics::DrawRoundedRect(fixedBounds, Theme::Current().ColourBorder(), 5, 5, 5, 5, surface);
        if (fixedBounds.Contains(window->lastMousePos)) {
            Lemon::Graphics::DrawRoundedRect(innerRect, Theme::Current().ColourForeground(), 5, 5, 5, 5, surface);
        } else {
            Lemon::Graphics::DrawRoundedRect(innerRect, Theme::Current().ColourButton(), 5, 5, 5, 5, surface);
        }
    }

    DrawButtonLabel(surface);
}

void Button::OnMouseDown(__attribute__((unused)) vector2i_t mousePos) { pressed = true; }

void Button::OnMouseUp(vector2i_t mousePos) {
    if (Graphics::PointInRect(fixedBounds, mousePos) && e.onPress.handler)
        e.onPress();

    pressed = false;
}

//////////////////////////
// Bitmap
//////////////////////////
Bitmap::Bitmap(rect_t _bounds) : Widget(_bounds) {
    surface.buffer = (uint8_t*)malloc(fixedBounds.size.x * fixedBounds.size.y * 4);
    surface.width = fixedBounds.size.x;
    surface.height = fixedBounds.size.y;
}

Bitmap::Bitmap(rect_t _bounds, surface_t* surf)
    : Widget({_bounds.pos, (vector2i_t){surf->width, surf->height}}, LayoutSize::Fixed, LayoutSize::Fixed) {
    surface = *surf;
}

void Bitmap::Paint(surface_t* surface) { Graphics::surfacecpy(surface, &this->surface, fixedBounds.pos); }

//////////////////////////
// Label
//////////////////////////
Label::Label(const char* _label, rect_t _bounds) : Widget(_bounds) { label = _label; }

void Label::Paint(surface_t* surface) {
    Graphics::DrawString(label.c_str(), fixedBounds.pos.x, fixedBounds.pos.y, textColour.r, textColour.g, textColour.b,
                         surface);
}

//////////////////////////
// Scroll Bar
//////////////////////////
void ScrollBar::ResetScrollBar(int displayHeight, int areaHeight) {
    double scrollDisplayRange =
        ((double)areaHeight) / displayHeight; // Essentially how many 'displayHeight's in areaHeight
    scrollIncrement = ceil(scrollDisplayRange);
    scrollBar.size.y = displayHeight / scrollDisplayRange;
    scrollBar.pos.y = 0;
    scrollPos = 0;
    height = displayHeight;
}

void ScrollBar::ScrollTo(int pos) {
    scrollPos = pos;
    scrollBar.pos.y = pos / scrollIncrement;
}

void ScrollBar::Paint(surface_t* surface, vector2i_t offset, int width) {
    Graphics::DrawRect(offset.x, offset.y, width, height, 128, 128, 128, surface);
    if (pressed)
        Graphics::DrawRect(offset.x, offset.y + scrollBar.pos.y, width, scrollBar.size.y,
                           Theme::Current().ColourBorder(), surface);
    else
        Graphics::DrawRect(offset.x, offset.y + scrollBar.pos.y, width, scrollBar.size.y,
                           Theme::Current().ColourButton(), surface);
}

void ScrollBar::OnMouseDownRelative(vector2i_t mousePos) {
    if (mousePos.y > scrollBar.pos.y && mousePos.y < scrollBar.pos.y + scrollBar.size.y) {
        pressed = true;
        pressOffset = mousePos.y - scrollBar.pos.y;
    }
}

void ScrollBar::OnMouseMoveRelative(vector2i_t relativePosition) {
    if (pressed) {
        scrollBar.pos.y = relativePosition.y - pressOffset;
        if (scrollBar.pos.y + scrollBar.size.y > height)
            scrollBar.pos.y = height - scrollBar.size.y;
        if (scrollBar.pos.y < 0)
            scrollBar.pos.y = 0;
        scrollPos = scrollBar.pos.y * scrollIncrement;
    }
}

void ScrollBarHorizontal::ResetScrollBar(int displayWidth, int areaWidth) {
    double scrollDisplayRange =
        ((double)areaWidth) / displayWidth; // Essentially how many 'displayHeight's in areaHeight
    scrollIncrement = ceil(scrollDisplayRange);
    scrollBar.size.x = displayWidth / scrollDisplayRange;
    scrollBar.pos.x = 0;
    scrollPos = 0;
    width = displayWidth;
}

void ScrollBarHorizontal::Paint(surface_t* surface, vector2i_t offset, int height) {
    Graphics::DrawRect(offset.x, offset.y, width, height, 128, 128, 128, surface);

    if (pressed)
        Graphics::DrawRect(offset.x + scrollBar.pos.x, offset.y, scrollBar.size.x, height, 224 / 1.1, 224 / 1.1,
                           219 / 1.1, surface);
    else
        Graphics::DrawGradient(offset.x + scrollBar.pos.x, offset.y, scrollBar.size.x, height, {250, 250, 250, 255},
                               {235, 235, 230, 255}, surface);
}

void ScrollBarHorizontal::OnMouseDownRelative(vector2i_t mousePos) {
    if (mousePos.x > scrollBar.pos.x && mousePos.x < scrollBar.pos.x + scrollBar.size.x) {
        pressed = true;
        pressOffset = mousePos.x - scrollBar.pos.x;
    }
}

void ScrollBarHorizontal::OnMouseMoveRelative(vector2i_t relativePosition) {
    if (pressed) {
        scrollBar.pos.x = relativePosition.x - pressOffset;
        if (scrollBar.pos.x + scrollBar.size.x > width)
            scrollBar.pos.x = width - scrollBar.size.x;
        if (scrollBar.pos.x < 0)
            scrollBar.pos.x = 0;
        scrollPos = scrollBar.pos.x * scrollIncrement;
    }
}

//////////////////////////
// Text Box
//////////////////////////
TextBox::TextBox(rect_t bounds, bool multiline) : Widget(bounds) {
    this->multiline = multiline;
    font = Graphics::GetFont("default");
    contents.push_back(std::string());

    {
        ContextMenuEntry ctx;
        ctx.id = TextboxCommandCut;
        ctx.name = "Cut";

        ctxEntries.push_back(ctx);
    }

    {
        ContextMenuEntry ctx;
        ctx.id = TextboxCommandCopy;
        ctx.name = "Copy";

        ctxEntries.push_back(ctx);
    }

    {
        ContextMenuEntry ctx;
        ctx.id = TextboxCommandPaste;
        ctx.name = "Paste";

        ctxEntries.push_back(ctx);
    }

    {
        ContextMenuEntry ctx;
        ctx.id = TextboxCommandDelete;
        ctx.name = "Delete";

        ctxEntries.push_back(ctx);
    }
}

void TextBox::Paint(surface_t* surface) {
    if (IsActive() || Graphics::PointInRect(fixedBounds, window->lastMousePos)) {
        Graphics::DrawRoundedRect(fixedBounds, Theme::Current().ColourForeground(), 3, 3, 3, 3, surface);
    } else {
        Graphics::DrawRoundedRect(fixedBounds, Theme::Current().ColourBorder(), 3, 3, 3, 3, surface);
    }

    Graphics::DrawRoundedRect(
        {fixedBounds.pos.x + 1, fixedBounds.pos.y + 1, fixedBounds.size.x - 2, fixedBounds.size.y - 2},
        Theme::Current().ColourContentBackground(), 3, 3, 3, 3, surface);

    int xpos = 2;
    int ypos = 2;
    int cursorY = 0;
    int cursorX = 0;

    if (multiline) {
        cursorY = cursorPos.y * font->lineHeight - sBar.scrollPos;

        for (size_t i = sBar.scrollPos / (font->lineHeight);
             i < contents.size() && i < static_cast<size_t>(sBar.scrollPos + fixedBounds.height) / font->lineHeight + 1;
             i++) {
            ypos = i * font->lineHeight + 2;

            for (size_t j = 0; j < contents[i].length(); j++) {
                if (contents[i][j] == '\t') {
                    xpos += font->tabWidth * font->width;
                } else if (isspace(contents[i][j])) {
                    xpos += font->width;
                } else if (!isgraph(contents[i][j])) {
                    continue;
                } else {
                    xpos += Graphics::DrawChar(contents[i][j], fixedBounds.pos.x + xpos,
                                               fixedBounds.pos.y + ypos - sBar.scrollPos, textColour.r, textColour.g,
                                               textColour.b, surface, fixedBounds, font);
                }

                if (cursorPos.y == static_cast<int>(i) && static_cast<int>(j) + 1 == cursorPos.x) {
                    cursorX = xpos;
                }

                if ((xpos > (fixedBounds.size.x - 8 - 16))) {
                    break;
                }
            }
            xpos = 0;
            if (ypos - sBar.scrollPos + font->lineHeight >= fixedBounds.size.y)
                break;
        }

        // Check if all lines can be displayed on screen
        // if not, draw the scrollbar
        if (static_cast<int>(contents.size()) * (font->lineHeight + 2) >= fixedBounds.size.y) {
            sBar.Paint(surface, {fixedBounds.pos.x + fixedBounds.size.x - 16, fixedBounds.pos.y});
        }
    } else {
        ypos = fixedBounds.height / 2 - font->height / 2;
        cursorY = ypos;

        std::string& line = contents.front();
        for (size_t j = 0; j < contents[0].length(); j++) {
            char ch;

            if (masked) {
                ch = '*';
            } else {
                ch = line[j];

                if (ch == '\t') {
                    xpos += font->tabWidth * font->width;
                    continue;
                } else if (isspace(ch)) {
                    xpos += font->width;
                    continue;
                } else if (!isgraph(ch))
                    continue;
            }

            xpos += Graphics::DrawChar(ch, fixedBounds.pos.x + xpos, fixedBounds.pos.y + ypos, textColour.r,
                                       textColour.g, textColour.b, surface, font);

            if (static_cast<int>(j) + 1 == cursorPos.x) {
                cursorX = xpos;
            }
        }
    }

    if (editable && parent->active == this) { // Only draw cursor if active
        timespec t;
        clock_gettime(CLOCK_BOOTTIME, &t);

        long msec = (t.tv_nsec / 1000000.0);
        if (msec < 250 || (msec > 500 && msec < 750)) // Only draw the cursor for a quarter of a second so it blinks
            Graphics::DrawRect(fixedBounds.pos.x + cursorX, fixedBounds.pos.y + cursorY, 2, font->lineHeight,
                               textColour.r, textColour.g, textColour.b, surface);
    }
}

void TextBox::LoadText(const char* text) {
    const char* text2 = text;
    int lineCount = 1;

    contents.clear();

    if (multiline) {
        while (*text2) {
            if (*text2 == '\n')
                lineCount++;
            text2++;
        }

        text2 = text;
        for (int i = 0; i < lineCount; i++) {
            const char* end = strchr(text2, '\n');
            if (end) {
                size_t len = (uintptr_t)(end - text2);
                if (len > strlen(text2))
                    break;

                contents.push_back(std::string(text2, len));

                text2 = end + 1;
            } else {
                contents.push_back(std::string(text2));
            }
        }

        this->lineCount = contents.size();
        ResetScrollBar();
    } else {
        contents.push_back(std::string(text2));
    }
}

void TextBox::OnMouseDown(vector2i_t mousePos) {
    assert(contents.size() <= INT_MAX);

    mousePos.x -= fixedBounds.pos.x;
    mousePos.y -= fixedBounds.pos.y;

    if (multiline && mousePos.x > fixedBounds.size.x - 16) {
        sBar.OnMouseDownRelative({mousePos.x + fixedBounds.size.x - 16, mousePos.y});
        return;
    }

    if (!editable)
        return;

    if (multiline) {
        cursorPos.y = (sBar.scrollPos + mousePos.y) / (font->lineHeight);
        if (cursorPos.y >= static_cast<int>(contents.size()))
            cursorPos.y = contents.size() - 1;
    }

    int dist = 0;
    for (cursorPos.x = 0; cursorPos.x < static_cast<int>(contents[cursorPos.y].length()); cursorPos.x++) {
        dist += Graphics::GetCharWidth(contents[cursorPos.y][cursorPos.x], font);
        if (dist >= mousePos.x) {
            break;
        }
    }
}

void TextBox::OnRightMouseDown(__attribute__((unused)) vector2i_t mousePos) {
    if (window) {
        window->DisplayContextMenu(ctxEntries);
    }
}

void TextBox::OnCommand(unsigned short cmd) { (void)cmd; }

void TextBox::OnMouseMove(__attribute__((unused)) vector2i_t mousePos) {
    if (multiline && sBar.pressed) {
        sBar.OnMouseMoveRelative({0, mousePos.y - fixedBounds.pos.y});
    }
}

void TextBox::OnMouseUp(__attribute__((unused)) vector2i_t mousePos) { sBar.pressed = false; }

void TextBox::ResetScrollBar() { sBar.ResetScrollBar(fixedBounds.size.y, contents.size() * (font->lineHeight)); }

void TextBox::OnKeyPress(int key) {
    if (!editable)
        return;

    assert(contents.size() < INT_MAX);

    if (isprint(key)) {
        contents[cursorPos.y].insert(cursorPos.x++, 1, key);
    } else if (key == '\b' || key == KEY_DELETE) {
        if (key == KEY_DELETE) { // Delete is essentially backspace but on the character in front so just increment the
                                 // cursor pos.
            cursorPos.x++;
            if (cursorPos.x > static_cast<int>(contents[cursorPos.y].length())) {
                if (cursorPos.y < static_cast<int>(contents.size()) - 1) {
                    cursorPos.x = static_cast<int>(contents[++cursorPos.y].length());
                } else
                    return;
            }
        }

        if (cursorPos.x) {
            contents[cursorPos.y].erase(--cursorPos.x, 1);
        } else if (cursorPos.y) { // Delete line if not at start of file
            cursorPos.x = static_cast<int>(
                contents[cursorPos.y - 1].length());            // Move cursor horizontally to end of previous line
            contents[cursorPos.y - 1] += contents[cursorPos.y]; // Append contents of current line to previous
            contents.erase(contents.begin() + cursorPos.y--);   // Remove line and move to previous line

            ResetScrollBar();
        }
    } else if (key == '\n') {
        if (multiline) {
            std::string s = contents[cursorPos.y].substr(cursorPos.x); // Grab the contents of the line after the cursor
            contents[cursorPos.y].erase(cursorPos.x);                  // Erase all that was after the cursor
            contents.insert(contents.begin() + (++cursorPos.y),
                            s); // Insert new line after cursor and move to that line
            cursorPos.x = 0;

            ResetScrollBar();
        } else if (OnSubmit) {
            OnSubmit(this);
        }
    } else if (key == KEY_ARROW_LEFT) { // Move cursor left
        cursorPos.x--;
        if (cursorPos.x < 0) {
            if (cursorPos.y) {
                cursorPos.x = static_cast<int>(contents[--cursorPos.y].length());
            } else
                cursorPos.x = 0;
        }
    } else if (key == KEY_ARROW_RIGHT) { // Move cursor right
        cursorPos.x++;
        if (cursorPos.x > static_cast<int>(contents[cursorPos.y].length())) {
            if (cursorPos.y < static_cast<int>(contents.size())) {
                cursorPos.x = 0;
                cursorPos.y++;
            } else
                cursorPos.x = contents[cursorPos.y].length();
        }
    } else if (key == KEY_ARROW_UP) { // Move cursor up
        if (cursorPos.y) {
            cursorPos.y--;
            if (cursorPos.x > static_cast<int>(contents[cursorPos.y].length())) {
                cursorPos.x = static_cast<int>(contents[cursorPos.y].length());
            }
        } else
            cursorPos.x = 0;
    } else if (key == KEY_ARROW_DOWN) { // Move cursor down
        if (cursorPos.y < static_cast<int>(contents.size())) {
            cursorPos.y++;
            if (cursorPos.x > static_cast<int>(contents[cursorPos.y].length())) {
                cursorPos.x = static_cast<int>(contents[cursorPos.y].length());
            }
        } else
            cursorPos.x = contents[cursorPos.y].length();
    }

    if (cursorPos.y >= static_cast<int>(contents.size())) {
        cursorPos.y = contents.size() - 1;
        cursorPos.x = contents[cursorPos.y].length() - 1;
    }

    if (cursorPos.y * font->lineHeight < sBar.scrollPos) {
        sBar.ScrollTo(cursorPos.y * font->lineHeight); // Make sure cursor is on screen
    } else if ((cursorPos.y + 1) * font->lineHeight > (sBar.scrollPos + fixedBounds.height)) {
        sBar.ScrollTo(cursorPos.y * font->lineHeight -
                      (fixedBounds.height - font->lineHeight)); // Make sure cursor is on screen
    }

    if (sBar.scrollPos < 0) {
        sBar.ScrollTo(0);
    }
}

void TextBox::MaskText(bool state) {
    if (!multiline) {
        masked = state;
    } else {
        masked = false;
    }
}

//////////////////////////
// ListView
//////////////////////////
ListView::ListView(rect_t bounds) : Widget(bounds) {
    font = Graphics::GetFont("default");

    assert(font); // Then shit really went down

    editbox.OnSubmit = [](Lemon::GUI::TextBox* tb) -> void {
        reinterpret_cast<ListView*>(tb->GetParent())->OnEditboxSubmit();
    };
}

ListView::~ListView() {}

void ListView::SetModel(DataModel* model) {
    this->model = model;

    model->Refresh();
    cacheDirty = true;
}

void ListView::UpdateData() {
    cacheDirty = true;
}

void ListView::Paint(surface_t* surface) {
    if (drawBackground) {
        Graphics::DrawRect(fixedBounds.x, fixedBounds.y, fixedBounds.width, columnDisplayHeight,
                           Theme::Current().ColourBackground(), surface);
        Graphics::DrawRect(fixedBounds.x, fixedBounds.y + columnDisplayHeight, fixedBounds.width,
                           fixedBounds.height - columnDisplayHeight, Theme::Current().ColourContentBackground(),
                           surface);
    }

    if (!model) {
        return; // If there is no model there is no data to display
    }

    if(cacheDirty) {
        RepopulateCache();
        cacheDirty = false;
    }

    rgba_colour_t textColour = Theme::Current().ColourText();

    int totalColumnWidth;
    int xPos = fixedBounds.x;
    for (int i = 0; i < model->ColumnCount(); i++) {
        if (displayColumnNames) {
            Graphics::DrawString(model->ColumnName(i), xPos + 4, fixedBounds.y + 4, textColour.r, textColour.g,
                                 textColour.b, surface);

            Graphics::DrawRect(xPos, fixedBounds.y + 1, 1, columnDisplayHeight - 2, textColour, surface); // Divider
            xPos++;
            Graphics::DrawRect(xPos, fixedBounds.y + 1, 1, columnDisplayHeight - 2, Theme::Current().ColourTextDark(),
                               surface); // Divider
            xPos++;
        }

        xPos += columnDisplayWidths.at(i);
    }

    totalColumnWidth = xPos;

    xPos = 0;
    int yPos = fixedBounds.y + (columnDisplayHeight *
                                displayColumnNames); // Offset by column display height only if we display the columns

    int index = 0;

    if (showScrollBar)
        index = sBar.scrollPos / itemHeight;
    int maxItem = std::min(index + rowsToDisplay, model->RowCount());

    for (int row = 0; index < maxItem; index++, row++) {
        xPos = fixedBounds.x;

        if (index == selected) {
            Graphics::DrawRect(xPos + 1, yPos + 1, totalColumnWidth - 2, itemHeight - 2,
                               Theme::Current().ColourForeground(), surface, fixedBounds);
        } else if (Graphics::PointInRect({xPos, yPos, fixedBounds.width, itemHeight}, window->lastMousePos)) {
            Graphics::DrawRect(xPos + 1, yPos + 1, totalColumnWidth - 2, itemHeight - 2,
                               Theme::Current().ColourForegroundInactive(), surface, fixedBounds);
        }

        for (int i = 0; i < model->ColumnCount(); i++) {
            std::string str = "";

            Variant value = model->GetData(index, i);
            if (std::holds_alternative<const Surface*>(value)) {
                const Surface* src = std::get<const Surface*>(value);
                Graphics::surfacecpyTransparent(surface, src, {xPos + 2, yPos + itemHeight / 2 - src->height / 2});
            } else if (i < (int)cachedRows.at(i).text.size()) {
                cachedRows.at(row).text[i].BlitTo(surface);
            }

            xPos += columnDisplayWidths[i] + 2;
        }
        yPos += itemHeight;
    }

    if (showScrollBar)
        sBar.Paint(surface,
                   fixedBounds.pos + (vector2i_t){fixedBounds.size.x, 0} - (vector2i_t){12, -columnDisplayHeight});

    if (editing) {
        editbox.Paint(surface);
    }
}

void ListView::OnMouseDown(vector2i_t mousePos) {
    if (!model) {
        return;
    }

    if (editing) {
        if (Graphics::PointInRect(editbox.GetFixedBounds(), mousePos)) {
            editbox.OnMouseDown(mousePos);
            return;
        } else {
            OnEditboxSubmit();
            editing = false;
        }
    }

    if (showScrollBar && mousePos.x > fixedBounds.pos.x + fixedBounds.size.x - 12) {
        sBar.OnMouseDownRelative({mousePos.x - fixedBounds.pos.x + fixedBounds.size.x - 12,
                                  mousePos.y - columnDisplayHeight - fixedBounds.pos.y});

        cacheDirty = true;
        return;
    }

    int lastSelected = selected;
    selected =
        floor(((double)mousePos.y + sBar.scrollPos - fixedBounds.pos.y - (displayColumnNames * columnDisplayHeight)) /
              itemHeight);

    if (lastSelected == selected) {
        int index = 0;
        int xPos = 0;
        for (int width : columnDisplayWidths) {
            if (mousePos.x - fixedBounds.x >= xPos && mousePos.x - fixedBounds.x <= xPos + width) {
                selectedCol = index;
                break; // We clicked on this column
            }

            xPos += width;
            index++;
        }

        /*if(selectedCol && col->editable){
            editbox.SetBounds({xPos, columnDisplayHeight + selected * itemHeight - sBar.scrollPos + 2,
        col->displayWidth, itemHeight - 4}); editing = true; editingColumnIndex = index;
            editbox.LoadText(items[selected].details[editingColumnIndex].c_str()); // Load with column data
        } else {
            editing = false;
        }*/
    }

    if (selected < 0)
        selected = 0;
    if (selected >= model->RowCount())
        selected = (int)model->RowCount() - 1;

    if (OnSelect)
        OnSelect(selected, this);
}

void ListView::OnDoubleClick(vector2i_t mousePos) {
    if (!model) {
        return;
    }

    if (!Graphics::PointInRect({fixedBounds.x, fixedBounds.y + columnDisplayHeight,
                                fixedBounds.width - (showScrollBar ? 12 : 0), fixedBounds.height - columnDisplayHeight},
                               mousePos)) {
        OnMouseDown(mousePos);
        return;
    } else {
        int clickedItem =
            floor(((double)mousePos.y + sBar.scrollPos - fixedBounds.pos.y - columnDisplayHeight) / itemHeight);

        if (selected == clickedItem) { // Make sure the same item was clicked twice
            if (OnSubmit)
                OnSubmit(selected, this);
        } else {
            selected = clickedItem;

            if (selected < 0)
                selected = 0;
            if (selected >= model->ColumnCount())
                selected = model->RowCount() - 1;
        }
    }
}

void ListView::OnMouseMove(vector2i_t mousePos) {
    if (showScrollBar && sBar.pressed) {
        sBar.OnMouseMoveRelative({0, mousePos.y - fixedBounds.pos.y});
    }
}

void ListView::OnMouseUp(__attribute__((unused)) vector2i_t mousePos) { sBar.pressed = false; }

void ListView::OnKeyPress(int key) {
    if (!model) {
        return;
    }

    if (editing) {
        editbox.OnKeyPress(key);
        return;
    }

    switch (key) {
    case KEY_ARROW_UP:
        selected--;
        break;
    case KEY_ARROW_DOWN:
        selected++;
        break;
    case KEY_ENTER:
        if (OnSubmit)
            OnSubmit(selected, this);
        return;
    }

    if (selected < 0)
        selected = 0;
    if (selected >= model->RowCount())
        selected = model->RowCount() - 1;
}

void ListView::OnInactive() {
    OnEditboxSubmit();
    editing = false;
}

void ListView::ResetScrollBar() {
    if (!model) {
        showScrollBar = false;
        return;
    }

    if ((model->RowCount() * itemHeight) > (fixedBounds.size.y - columnDisplayHeight))
        showScrollBar = true;
    else
        showScrollBar = false;

    sBar.ResetScrollBar(fixedBounds.size.y - columnDisplayHeight, model->RowCount() * itemHeight);
}

void ListView::RepopulateCache() {
    columnDisplayWidths.clear();

    for (int i = 0; i < model->ColumnCount(); i++) {
        columnDisplayWidths.push_back(model->SizeHint(i));
    }

    int index = 0;
    if (showScrollBar)
        index = sBar.scrollPos / itemHeight;
    int xPos = 0;
    int yPos = fixedBounds.y + (columnDisplayHeight *
                                displayColumnNames); // Offset by column display height only if we display the columns

    rowsToDisplay = fixedBounds.height / itemHeight;
    cachedRows.resize(rowsToDisplay);

    for (int row = 0; row < rowsToDisplay && row + index < model->RowCount(); row++) {
        xPos = fixedBounds.x;
        cachedRows[row].text.resize(model->ColumnCount());

        for (int i = 0; i < model->ColumnCount(); i++) {
            std::string str = "";

            Variant value = model->GetData(index + row, i);
            if (std::holds_alternative<std::string>(value)) {
                str = std::get<std::string>(value);
            } else if (std::holds_alternative<int>(value)) {
                str = std::to_string(std::get<int>(value));
            } else if (std::holds_alternative<const Surface*>(value)) {
                xPos += columnDisplayWidths[i] + 2;
                continue;
            } else {
                assert(!"GUI::ListView: Unsupported type!");
            }

            int textLen = Graphics::GetTextLength(str.c_str());
            if (textLen > columnDisplayWidths[i] - 2) {
                int l = str.length() - 1;
                while (l) {
                    str = str.substr(0, l);

                    textLen = Graphics::GetTextLength(str.c_str());
                    if (textLen < columnDisplayWidths[i] + 2) {
                        if (l > 2) {
                            str.erase(str.end() - 1); // Omit last character
                            str.append(
                                "..."); // We have a variable width font should we should only have to omit 1 character
                        }
                        break;
                    }

                    l = str.length() - 1;
                }
            }

            vector2i_t textPos = {xPos + 2, yPos + itemHeight / 2 - font->height / 2};
            cachedRows[row].text[i].SetPos(textPos);
            cachedRows[row].text[i].SetText(str);
            cachedRows[row].text[i].SetColour(Theme::Current().ColourTextDark());

            xPos += columnDisplayWidths[i] + 2;
        }
        yPos += itemHeight;
    }
}

void ListView::UpdateFixedBounds() {
    Widget::UpdateFixedBounds();

    cacheDirty = true;
    ResetScrollBar();
}

void ListView::OnEditboxSubmit() {
    if (!editing)
        return;

    // items[selected].details[editingColumnIndex] = editbox.contents.front();

    if (OnEdit) {
        OnEdit(selected, this);
    }

    editing = false;
}

void GridView::ResetScrollBar() {
    if (!items.size())
        return; // Lets not divide by zero

    assert(items.size() < INT32_MAX);

    if ((static_cast<int>(items.size() + itemsPerRow - 1) / itemsPerRow * itemSize.y) > fixedBounds.size.y)
        showScrollBar = true;
    else
        showScrollBar = false;

    if (showScrollBar)
        sBar.ResetScrollBar(fixedBounds.size.y, ((items.size() + itemsPerRow - 1) / itemsPerRow) * itemSize.y);
}

void GridView::Paint(surface_t* surface) {
    Graphics::DrawRect(fixedBounds, Theme::Current().ColourContentBackground(), surface);
    if (Graphics::PointInRect(fixedBounds, window->lastMousePos)) {
        Graphics::DrawRectOutline(fixedBounds, Theme::Current().ColourForeground(), surface);
    } else {
        Graphics::DrawRectOutline(fixedBounds, Theme::Current().ColourBorder(), surface);
    }

    int xPos = 0;
    int yPos = sBar.scrollPos ? -(sBar.scrollPos % itemSize.y) : 0;

    unsigned idx = 0;
    idx = sBar.scrollPos / itemSize.y * itemsPerRow;

    for (; idx < items.size() && yPos < fixedBounds.height; idx++) {
        GridItem& item = items[idx];

        vector2i_t iconPos = fixedBounds.pos + (vector2i_t){xPos + itemSize.x / 2 - 32, yPos + 2};
        if (item.icon) {
            rect_t srcRegion = {0, 0, 64, 64};

            int containerBottom = fixedBounds.pos.y + fixedBounds.size.y;
            if (iconPos.y < fixedBounds.pos.y) {
                srcRegion.height += (iconPos.y - fixedBounds.pos.y);
                srcRegion.y -= (iconPos.y - fixedBounds.pos.y);
                iconPos.y = fixedBounds.pos.y;
            } else if (iconPos.y > containerBottom) {
                continue;
            }

            if (iconPos.y + srcRegion.height > containerBottom) {
                srcRegion.height = containerBottom - iconPos.y;
            }

            Graphics::surfacecpyTransparent(surface, item.icon, iconPos, srcRegion);
        }

        std::string str = item.name;
        int len = Graphics::GetTextLength(str.c_str());
        if (static_cast<int>(idx) != selected && len > itemSize.x - 2) {
            int l = UTF8Strlen(str) - 1;
            while (l) {
                str = str.substr(0, UTF8SkipCodepoints(str, l));
                len = Graphics::GetTextLength(str.c_str());

                if (len < itemSize.x + 2) {
                    if (l > 2) {
                        str.erase(str.begin() + UTF8SkipCodepoints(str, -1)); // Omit last character
                        str.append(
                            "..."); // We have a variable width font should we should only have to omit 1 character
                    }
                    break;
                }

                l = UTF8Strlen(str) - 1;
            }
        }

        int fontHeight = Graphics::DefaultFont()->height;
        int textY = fixedBounds.y + yPos + itemSize.y - fontHeight - 4;
        int textX = fixedBounds.x + xPos + (itemSize.x / 2) - (len / 2);

        if (static_cast<int>(idx) == selected) {
            Graphics::DrawRect(textX - 4, textY, len + 7, Graphics::DefaultFont()->height + 7,
                               Theme::Current().ColourForeground(), surface,
                               fixedBounds); // Highlight the label if selected
        } else if (Graphics::PointInRect({{textX, textY}, {len, fontHeight}}, window->lastMousePos)) {
            Graphics::DrawRect(textX - 4, textY, len + 7, Graphics::DefaultFont()->height + 7,
                               Theme::Current().ColourForegroundInactive(), surface,
                               fixedBounds); // Highlight the label if moused over
        } else if (item.icon &&
                   Graphics::PointInRect({iconPos, {item.icon->width, item.icon->height}}, window->lastMousePos)) {
            Graphics::DrawRect(textX - 4, textY, len + 7, Graphics::DefaultFont()->height + 7,
                               Theme::Current().ColourForegroundInactive(), surface,
                               fixedBounds); // Highlight the label if moused over
        }

        Graphics::DrawString(str.c_str(), textX, textY, Theme::Current().ColourText(), surface, fixedBounds);

        xPos += itemSize.x;

        if (xPos + itemSize.x > fixedBounds.width) {
            xPos = 0;
            yPos += itemSize.y;
        }

        if (yPos >= fixedBounds.height) {
            break;
        }
    }

    if (showScrollBar) {
        sBar.Paint(surface, {fixedBounds.x + fixedBounds.width - 12, fixedBounds.y});
    }
}

void GridView::OnMouseDown(vector2i_t mousePos) {
    mousePos -= fixedBounds.pos;

    if (showScrollBar &&
        Graphics::PointInRect((rect_t){{fixedBounds.width - 12, 0}, {12, fixedBounds.height}}, mousePos)) {
        sBar.OnMouseDownRelative(mousePos);
    } else {
        selected = PosToItem(
            mousePos +
            (vector2i_t){
                0, sBar.scrollPos}); // Position relative to position of GridView, but absolute from scroll position

        if (OnSelect)
            OnSelect(items[selected], this);
    }
}

void GridView::OnMouseUp(vector2i_t mousePos) {
    (void)mousePos;

    sBar.pressed = false;
}

void GridView::OnMouseMove(vector2i_t mousePos) {
    vector2i_t sBarPos = {fixedBounds.x + fixedBounds.width - 16, fixedBounds.y};
    if (sBar.pressed) {
        sBar.OnMouseMoveRelative(mousePos - sBarPos);
    }
}

void GridView::OnDoubleClick(vector2i_t mousePos) {
    int oldSelected = selected;
    selected = PosToItem(
        mousePos - fixedBounds.pos +
        (vector2i_t){0,
                     sBar.scrollPos}); // Position relative to position of GridView, but absolute from scroll position

    if (selected >= 0 && oldSelected == selected && static_cast<unsigned>(selected) < items.size()) {
        if (OnSubmit)
            OnSubmit(items[selected], this);
    }
}

void GridView::OnKeyPress(int key) {
    if (key == '\n') {
        if (selected >= 0 && static_cast<unsigned>(selected) < items.size()) {
            if (OnSubmit)
                OnSubmit(items[selected], this);
        }
    } else if (key == KEY_ARROW_UP) {
        if (selected / itemsPerRow > 0) {
            selected -= itemsPerRow;

            if (selected >= 0 && static_cast<unsigned>(selected) < items.size()) {
                if (OnSelect)
                    OnSelect(items[selected], this);
            }
        }
    } else if (key == KEY_ARROW_LEFT) {
        if (selected > 0) {
            selected--;

            if (selected >= 0 && static_cast<unsigned>(selected) < items.size()) {
                if (OnSelect)
                    OnSelect(items[selected], this);
            }
        }
    } else if (key == KEY_ARROW_DOWN) {
        if (selected / itemsPerRow < static_cast<int>(items.size()) / itemsPerRow) {
            selected += itemsPerRow;

            if (selected >= static_cast<int>(items.size())) {
                selected = items.size() - 1;
            }

            if (selected >= 0 && static_cast<unsigned>(selected) < items.size()) {
                if (OnSelect)
                    OnSelect(items[selected], this);
            }
        }
    } else if (key == KEY_ARROW_RIGHT) {
        if (selected < static_cast<int>(items.size()) - 1) {
            selected++;

            if (selected >= 0 && static_cast<unsigned>(selected) < items.size()) {
                if (OnSelect)
                    OnSelect(items[selected], this);
            }
        }
    }

    if (selected >= 0 && static_cast<unsigned>(selected) < items.size()) {
        if (selected >= 0 && ((selected / itemsPerRow + 1) * itemSize.y) >
                                 sBar.scrollPos + fixedBounds.height) { // Check if bottom of item is in view
            sBar.ScrollTo(((selected / itemsPerRow + 1) * itemSize.y) -
                          fixedBounds.height); // Scroll down to selected item
        }

        if (selected >= 0 && (selected / itemsPerRow * itemSize.y) < sBar.scrollPos) {
            sBar.ScrollTo(selected / itemsPerRow * itemSize.y); // Scroll up to selected item
        }
    }
}

int GridView::AddItem(GridItem& item) {
    items.push_back(item);

    ResetScrollBar();

    return items.size() - 1;
}

void GridView::UpdateFixedBounds() {
    Widget::UpdateFixedBounds();

    if (fixedBounds.width <= 0) {
        return;
    }

    assert(itemSize.x);

    itemsPerRow = std::max(1, fixedBounds.width / itemSize.x);

    ResetScrollBar();
}

void ScrollView::Paint(surface_t* surface) {
    for (Widget* w : children) {
        w->Paint(surface);
    }

    sBarVertical.Paint(surface, fixedBounds.pos + (vector2i_t){fixedBounds.size.x - 16, 0});
    sBarHorizontal.Paint(surface, fixedBounds.pos + (vector2i_t){0, fixedBounds.size.y - 16});
}

void ScrollView::AddWidget(Widget* w) {
    children.push_back(w);

    w->SetParent(this);
    w->SetWindow(window);

    UpdateFixedBounds();
}

void ScrollView::OnMouseDown(vector2i_t mousePos) {
    mousePos -= fixedBounds.pos;
    if (mousePos.x >= fixedBounds.width - 16) {
        sBarVertical.OnMouseDownRelative(mousePos - (vector2i_t){fixedBounds.width - 16, 0});
    } else if (mousePos.y >= fixedBounds.height - 16) {
        sBarHorizontal.OnMouseDownRelative(mousePos - (vector2i_t){0, fixedBounds.height - 16});
    } else
        for (Widget* w : children) {
            if (Graphics::PointInRect(w->GetFixedBounds(), mousePos)) {
                active = w;
                w->OnMouseDown(mousePos);
                break;
            }
        }
}

void ScrollView::OnMouseUp(vector2i_t mousePos) {
    sBarHorizontal.pressed = sBarVertical.pressed = false;

    if (active) {
        active->OnMouseUp(mousePos);
    }

    UpdateFixedBounds();
}

void ScrollView::OnMouseMove(vector2i_t mousePos) {
    if (sBarVertical.pressed) {
        sBarVertical.OnMouseMoveRelative(mousePos - (vector2i_t){fixedBounds.width - 16, 0});
        UpdateFixedBounds();
    } else if (sBarHorizontal.pressed) {
        sBarHorizontal.OnMouseMoveRelative(mousePos - (vector2i_t){0, fixedBounds.height - 16});
        UpdateFixedBounds();
    } else if (active) {
        active->OnMouseMove(mousePos);
    }
}

void ScrollView::UpdateFixedBounds() {
    Widget::UpdateFixedBounds();

    rect_t realFixedBounds = fixedBounds;
    fixedBounds.pos += {-sBarHorizontal.scrollPos, -sBarVertical.scrollPos};
    fixedBounds.size += {sBarHorizontal.scrollPos, sBarVertical.scrollPos};

    for (Widget* w : children) {
        w->UpdateFixedBounds();

        if (w->GetFixedBounds().width + w->GetFixedBounds().x > scrollBounds.width) {
            scrollBounds.width = w->GetFixedBounds().width + (w->GetFixedBounds().x - fixedBounds.x);
        }

        if (w->GetFixedBounds().height + w->GetFixedBounds().y > scrollBounds.height) {
            scrollBounds.height = w->GetFixedBounds().height + (w->GetFixedBounds().y - fixedBounds.y);
        }
    }

    fixedBounds = realFixedBounds;

    sBarVertical.ResetScrollBar(fixedBounds.height - 12, scrollBounds.height);
    sBarHorizontal.ResetScrollBar(fixedBounds.width - 12, scrollBounds.width);
}
} // namespace Lemon::GUI