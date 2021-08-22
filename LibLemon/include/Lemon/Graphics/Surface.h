#pragma once

#include <stdint.h>

#include <Lemon/Graphics/Rect.h>
#include <Lemon/Graphics/Vector.h>

typedef struct Surface {
    int width = 0;
    int height = 0;            // Self-explanatory
    uint8_t depth = 32;        // Pixel depth, unsupported
    uint8_t* buffer = nullptr; // Start of the buffer

    // Returns buffer size in bytes
    inline unsigned BufferSize() const { return width * height * (depth >> 3); }

    void Blit(const Surface* src);
    void Blit(const Surface* src, const Vector2i& offset);
    void Blit(const Surface* src, const Vector2i& offset, const struct Rect& region);
    void AlphaBlit(const Surface* src, const Vector2i& offset, const Rect& region = {0, 0, INT_MAX, INT_MAX});
} surface_t;
