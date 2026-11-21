#pragma once
#include <cstdint>

namespace bdg {
class Window;

// RHI: engine code never calls a graphics API directly. First backend: OpenGL 4.5.
struct Color { float r, g, b, a; };

class RHI {
public:
  bool Init(Window& window);
  void BeginFrame(int32_t width, int32_t height, Color clear);
  void EndFrame();
};
}
