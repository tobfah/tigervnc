/* Copyright 2026 Adam Halim for Cendio AB
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifndef __WAYLAND_PIXELBUFFER_H__
#define __WAYLAND_PIXELBUFFER_H__

#include <functional>
#include <vector>

#include <rfb/PixelBuffer.h>

namespace rfb { class VNCServer; class PixelFormat; }

namespace wayland {
  class Output;
  class Display;
  class ScreencopyManager;
  class OutputImageCaptureSourceManager;
  class ImageCaptureSource;
  class Seat;
  class ImageCopyCaptureManager;
};

class WaylandPixelBuffer : public rfb::ManagedPixelBuffer {
public:
  WaylandPixelBuffer(wayland::Display* display, wayland::Output* output,
                     wayland::Seat* seat, rfb::VNCServer* server,
                     std::function<void()> desktopReadyCallback);
  ~WaylandPixelBuffer();

protected:
  // Called when there is pixel data available to read
  void bufferEvent(uint8_t* buffer, core::Region damage, rfb::PixelFormat pf);

private:
  void imageCopyBufferEvent(uint8_t* buffer, core::Region damage,
                            uint32_t shmFormat);
  void cursorImageEvent(int width, int height, const core::Point& hotspot,
                        uint32_t shmFormat, const uint8_t* src);
  void cursorPosEvent(const core::Point& pos);
  rfb::PixelFormat convertFormat(uint32_t shmFormat);
  bool pickShmFormat(const std::vector<uint32_t>& formats, uint32_t* out);
  void convertCursorBuffer(const uint8_t* src, uint32_t format,
                           uint32_t width, uint32_t height,
                           std::vector<uint8_t>& out);

  // Sync the shadow framebuffer to the actual framebuffer
  void syncBuffers(uint8_t* buffer, core::Region damage);

private:
  bool firstFrame;
  std::function<void()> desktopReadyCallback;
  rfb::VNCServer* server;
  wayland::Display* display;
  wayland::Output* output;
  wayland::ScreencopyManager* screencopyManager;
  wayland::OutputImageCaptureSourceManager* outputImageCaptureSourceManager;
  wayland::ImageCaptureSource* imageCaptureSource;
  wayland::ImageCopyCaptureManager* imageCopyCaptureManager;
  bool resized;
};

#endif // __WAYLAND_PIXELBUFFER_H__
