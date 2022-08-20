#include <stdint.h>

#include <MM/VMObject.h>
#include <RefPtr.h>
#include <MiscHdr.h>

namespace Video{
    void Initialize(video_mode_t videoMode);
    video_mode_t GetVideoMode();
    FancyRefPtr<VMObject> GetFramebufferVMO();

    void DrawRect(unsigned int x, unsigned int y, unsigned int width, unsigned int height, uint8_t r, uint8_t g, uint8_t b);
    void DrawChar(char c, unsigned int x, unsigned int y, uint8_t r, uint8_t g, uint8_t b, int vscale = 1, int hscale = 1);
    void DrawString(const char* str, unsigned int x, unsigned int y, uint8_t r, uint8_t g, uint8_t b, int vscale = 1, int hscale = 1);

    void DrawBitmapImage(unsigned int x, unsigned int y, unsigned int w, unsigned int h, uint8_t *data);
}