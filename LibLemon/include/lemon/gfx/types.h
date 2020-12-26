#pragma once

#include <list>

typedef struct Vector2i{
    int x, y;
} vector2i_t; // Two dimensional integer vector

inline vector2i_t operator+ (const vector2i_t& l, const vector2i_t& r){
    return {l.x + r.x, l.y + r.y};
}

inline void operator+= (vector2i_t& l, const vector2i_t& r){
    l = l + r;
}

inline vector2i_t operator- (const vector2i_t& l, const vector2i_t& r){
    return {l.x - r.x, l.y - r.y};
}

inline void operator-= (vector2i_t& l, const vector2i_t& r){
    l = l - r;
}

typedef struct Rect{
    union {
        vector2i_t pos;
        struct {
            int x;
            int y;
        };
    };
    union {
        vector2i_t size;
        struct {
            int width;
            int height;
        };
    };

    int left() { return x; }
    int right() { return x + width; }
    int top() { return y; }
    int bottom() { return y + height; }

    int left(int newLeft) { width += x - newLeft; x = newLeft; return x; }
    int right(int newRight) { width += newRight - (x + width); return x + width; }
    int top(int newTop) { height += y - newTop; y = newTop; return y; }
    int bottom(int newBottom) { height += newBottom - (y + height); return y + height; }

    std::list<Rect> Split(Rect cut){
        std::list<Rect> clips;
        Rect victim = *this;

        if(cut.left() >= victim.left() && cut.left() <= victim.right()) { // Clip left edge
            Rect clip;
            clip.top(victim.top());
            clip.left(victim.left());
            clip.bottom(victim.bottom());
            clip.right(cut.left()); // Left of cutting rect's left edge

            victim.left(cut.left());

            clips.push_back(clip);
        }

        if(cut.top() >= victim.top() && cut.top() <= victim.bottom()) { // Clip top edge
            Rect clip;
            clip.top(victim.top());
            clip.left(victim.left());
            clip.bottom(cut.top()); // Above cutting rect's top edge
            clip.right(victim.right());

            victim.top(cut.top());

            clips.push_back(clip);
        }

        if(cut.right() >= victim.left() && cut.right() <= victim.right()) { // Clip right edge
            Rect clip;
            clip.top(victim.top());
            clip.left(cut.right());
            clip.bottom(victim.bottom());
            clip.right(victim.right());

            victim.right(cut.right());

            clips.push_back(clip);
        }

        if(cut.bottom() >= victim.top() && cut.bottom() <= victim.bottom()) { // Clip bottom edge
            Rect clip;
            clip.top(cut.bottom());
            clip.left(victim.left());
            clip.bottom(victim.bottom());
            clip.right(victim.right());

            victim.bottom(cut.bottom());

            clips.push_back(clip);
        }

        return clips;
    }
} rect_t; // Rectangle

typedef struct RGBAColour{
    uint8_t r, g, b, a; /* Red, Green, Blue, Alpha (Transparency) Respectively*/
} __attribute__ ((packed)) rgba_colour_t;