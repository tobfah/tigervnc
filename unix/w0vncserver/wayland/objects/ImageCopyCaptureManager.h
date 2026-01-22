/* Copyright 2026 Tobias Fahleson for Cendio AB
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

#ifndef __WAYLAND_IMAGE_COPY_CAPTURE_MANAGER_H__
#define __WAYLAND_IMAGE_COPY_CAPTURE_MANAGER_H__

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <vector>

#include <core/Region.h>
#include "Object.h"

struct wl_array;
struct wl_buffer;
struct wl_pointer;

struct ext_image_capture_source_v1;
struct ext_image_copy_capture_manager_v1;
struct ext_image_copy_capture_session_v1;
struct ext_image_copy_capture_frame_v1;
struct ext_image_copy_capture_cursor_session_v1;
struct ext_image_copy_capture_session_v1_listener;
struct ext_image_copy_capture_frame_v1_listener;
struct ext_image_copy_capture_cursor_session_v1_listener;

namespace wayland {
  class Display;
  class Shm;
  class ShmPool;

  class ImageCopyCaptureSession;
  class ImageCopyCaptureCursorSession;

  class ImageCopyCaptureManager : public Object {
  public:
    ImageCopyCaptureManager(
      Display* display,
      ext_image_capture_source_v1* source,
      wl_pointer* pointer,
      std::function<void(uint8_t*, core::Region, uint32_t)>
        bufferEventCb,
      std::function<bool(const std::vector<uint32_t>&, uint32_t*)>
        pickShmFormatCb,
      std::function<void(int, int, const core::Point&, uint32_t,
                         const uint8_t*)>
        cursorImageCb,
      std::function<void(const core::Point&)> cursorPosCb,
      std::function<void()> stoppedCb);
    ~ImageCopyCaptureManager();

    void createSession();
    void createPointerCursorSession();

  private:
    ext_image_copy_capture_manager_v1* manager;
    Display* display;
    ext_image_capture_source_v1* source;
    ImageCopyCaptureSession* session;
    wl_pointer* pointer;
    ImageCopyCaptureCursorSession* cursorSession;
    std::function<void(uint8_t*, core::Region, uint32_t)>
      bufferEventCb;
    std::function<bool(const std::vector<uint32_t>&, uint32_t*)>
      pickShmFormatCb;
    std::function<void(int, int, const core::Point&, uint32_t,
                       const uint8_t*)>
      cursorImageCb;
    std::function<void(const core::Point&)> cursorPosCb;
    std::function<void()> stoppedCb;
    bool active;
    void stopped();
  };

  class ImageCopyCaptureSession {
  public:
    ImageCopyCaptureSession(
      Display* display,
      ext_image_copy_capture_session_v1* session,
      std::function<void(const ImageCopyCaptureSession&)> frameReadyCb,
      std::function<bool(const std::vector<uint32_t>&, uint32_t*)>
        pickShmFormatCb,
      std::function<void()> stoppedCb);
    ~ImageCopyCaptureSession();
    uint8_t* getData() const;
    uint32_t getWidth() const;
    uint32_t getHeight() const;
    uint32_t getFormat() const;
    const core::Region& getDamage() const;

  private:
    void handleBufferSize(uint32_t width, uint32_t height);
    void handleShmFormat(uint32_t format);
    void handleDmabufDevice(wl_array* device);
    void handleDmabufFormat(uint32_t format, wl_array* modifiers);
    void handleDone();
    void handleStopped();
    void handleFrameTransform(uint32_t transform);
    void handleFrameDamage(int32_t x, int32_t y, int32_t width,
                           int32_t height);
    void handleFramePresentationTime(uint32_t tvSecHi, uint32_t tvSecLo,
                                     uint32_t tvNsec);
    void handleFrameReady();
    void handleFrameFailed(uint32_t reason);
    void createFrame();
    bool initPool(const char* name, size_t size);

  private:
    Display* display;
    std::function<void(const ImageCopyCaptureSession&)> frameReadyCb;
    std::function<bool(const std::vector<uint32_t>&, uint32_t*)>
      pickShmFormatCb;
    ext_image_copy_capture_session_v1* session;
    ext_image_copy_capture_frame_v1* frame;
    Shm* shm;
    ShmPool* pool;
    wl_buffer* buffer;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    bool hasSize;
    bool constraintsReady;
    bool failed;
    std::function<void()> stoppedCb;
    std::vector<uint32_t> formats;
    core::Region damage;
    static const ext_image_copy_capture_session_v1_listener listener;
    static const ext_image_copy_capture_frame_v1_listener frameListener;
  };

  class ImageCopyCaptureCursorSession {
  public:
    ImageCopyCaptureCursorSession(
      Display* display,
      ext_image_copy_capture_cursor_session_v1* session,
      std::function<void(const ImageCopyCaptureSession&,
                         const core::Point&)> cursorFrameCb,
      std::function<void(const core::Point&)> cursorPosCb,
      std::function<bool(const std::vector<uint32_t>&, uint32_t*)>
        pickShmFormatCb,
      std::function<void()> stoppedCb);
    ~ImageCopyCaptureCursorSession();

  private:
    void handleEnter();
    void handleLeave();
    void handlePosition(int32_t x, int32_t y);
    void handleHotspot(int32_t x, int32_t y);

  private:
    std::function<void(const ImageCopyCaptureSession&,
                       const core::Point&)> cursorFrameCb;
    std::function<void(const core::Point&)> cursorPosCb;
    std::function<bool(const std::vector<uint32_t>&, uint32_t*)>
      pickShmFormatCb;
    ext_image_copy_capture_cursor_session_v1* session;
    ImageCopyCaptureSession* captureSession;
    core::Point cursorHotspot;
    static const ext_image_copy_capture_cursor_session_v1_listener
      listener;
  };
};

#endif // __WAYLAND_IMAGE_COPY_CAPTURE_MANAGER_H__
