#ifndef _GFX_SURFACE
#define _GFX_SURFACE

enum SurfaceFormats {
  SURF_ARGB,
  SURF_YUV,
};

struct cairo_surface_t;

class Surface {
 public:
  Surface(int width, int height);

  // In case of YUV, U and V channels have half size, rounded UP.
  int height() const { return height_; }
  int width()  const { return width_; }
  int pitch()  const { return pitch_; }

  int half_width()  const { return (width_ + 1) >> 1; }
  int half_height() const { return (height_ + 1) >> 1; }

  cairo_surface_t *CreateCairoSurface();
 private:
  uint8 *data_;
  int width_;
  int height_;
  int pitch_;
};

#endif
