#pragma once

#include <core/keyboard.h>
#include <gfx/graphics.h>

struct KeyboardState{
	bool caps, control, shift, alt;
};

void OnKey(int key);

class InputManager{

public:
    KeyboardState keyboard;

    void Poll();
};