#pragma once

#include <exception>

namespace Lemon::Graphics {
struct Font {
    bool monospace = false;
    void* face;
    int height;
    int pixelHeight;
    int lineHeight;
    int width;
    int tabWidth = 4;
    char* id;
};

class FontException : public std::exception {
  public:
    enum {
        FontUnknownError = 0,
        FontFileError = 1,
        FontLoadError = 2,
        FontSizeError = 3,
        FontRenderError = 4,
    };

    static const char* errorStrings[];

  protected:
    int errorType = FontUnknownError;
    int errorCode = 0;

  public:
    FontException(int errorType) { this->errorType = errorType; }

    FontException(int errorType, int errorCode) : FontException(errorType) { this->errorCode = errorCode; }

    const char* what() const noexcept { return errorStrings[errorType]; }
};

void InitializeFonts();
void RefreshFonts();
Font* LoadFont(const char* path, const char* id = nullptr, int sz = 10);
Font* GetFont(const char* id);

Font* DefaultFont();
} // namespace Lemon::Graphics
