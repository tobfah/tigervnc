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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdexcept>

#include <ext-image-copy-capture-v1.h>

#include <core/LogWriter.h>
#include <core/Rect.h>
#include <core/string.h>

#include "../../w0vncserver.h"
#include "Display.h"
#include "Object.h"
#include "Shm.h"
#include "ShmPool.h"
#include "ImageCopyCaptureManager.h"

using namespace wayland;

static core::LogWriter vlog("WaylandImageCopyCapture");

const ext_image_copy_capture_session_v1_listener
ImageCopyCaptureSession::listener = {
  .buffer_size = [](void* data, ext_image_copy_capture_session_v1*,
                    uint32_t width, uint32_t height) {
    ((ImageCopyCaptureSession*)data)->handleBufferSize(width, height);
  },
  .shm_format = [](void* data, ext_image_copy_capture_session_v1*,
                   uint32_t format) {
    ((ImageCopyCaptureSession*)data)->handleShmFormat(format);
  },
  .dmabuf_device = [](void* data, ext_image_copy_capture_session_v1*,
                      wl_array* device) {
    ((ImageCopyCaptureSession*)data)->handleDmabufDevice(device);
  },
  .dmabuf_format = [](void* data, ext_image_copy_capture_session_v1*,
                      uint32_t format, wl_array* modifiers) {
    ((ImageCopyCaptureSession*)data)->handleDmabufFormat(format,
                                                         modifiers);
  },
  .done = [](void* data, ext_image_copy_capture_session_v1*) {
    ((ImageCopyCaptureSession*)data)->handleDone();
  },
  .stopped = [](void* data, ext_image_copy_capture_session_v1*) {
    ((ImageCopyCaptureSession*)data)->handleStopped();
  },
};

const ext_image_copy_capture_frame_v1_listener
ImageCopyCaptureSession::frameListener = {
  .transform = [](void* data, ext_image_copy_capture_frame_v1*,
                  uint32_t transform) {
    ((ImageCopyCaptureSession*)data)->handleFrameTransform(transform);
  },
  .damage = [](void* data, ext_image_copy_capture_frame_v1*,
               int32_t x, int32_t y, int32_t width, int32_t height) {
    ((ImageCopyCaptureSession*)data)->handleFrameDamage(x, y, width,
                                                        height);
  },
  .presentation_time = [](void* data, ext_image_copy_capture_frame_v1*,
                          uint32_t tvSecHi, uint32_t tvSecLo,
                          uint32_t tvNsec) {
    ((ImageCopyCaptureSession*)data)->handleFramePresentationTime(tvSecHi,
                                                                  tvSecLo,
                                                                  tvNsec);
  },
  .ready = [](void* data, ext_image_copy_capture_frame_v1*) {
    ((ImageCopyCaptureSession*)data)->handleFrameReady();
  },
  .failed = [](void* data, ext_image_copy_capture_frame_v1*,
               uint32_t reason) {
    ((ImageCopyCaptureSession*)data)->handleFrameFailed(reason);
  },
};

const ext_image_copy_capture_cursor_session_v1_listener
ImageCopyCaptureCursorSession::listener = {
  .enter = [](void* data, ext_image_copy_capture_cursor_session_v1*) {
    ((ImageCopyCaptureCursorSession*)data)->handleEnter();
  },
  .leave = [](void* data, ext_image_copy_capture_cursor_session_v1*) {
    ((ImageCopyCaptureCursorSession*)data)->handleLeave();
  },
  .position = [](void* data, ext_image_copy_capture_cursor_session_v1*,
                 int32_t x, int32_t y) {
    ((ImageCopyCaptureCursorSession*)data)->handlePosition(x, y);
  },
  .hotspot = [](void* data, ext_image_copy_capture_cursor_session_v1*,
                int32_t x, int32_t y) {
    ((ImageCopyCaptureCursorSession*)data)->handleHotspot(x, y);
  },
};

ImageCopyCaptureManager::ImageCopyCaptureManager(
    Display* display_, ext_image_capture_source_v1* source_,
    wl_pointer* pointer_,
    std::function<void(uint8_t*, core::Region, uint32_t)>
      bufferEventCb_,
    std::function<bool(const std::vector<uint32_t>&, uint32_t*)>
      pickShmFormatCb_,
    std::function<void(int, int, const core::Point&, uint32_t,
                       const uint8_t*)>
      cursorImageCb_,
    std::function<void(const core::Point&)> cursorPosCb_,
    std::function<void()> stoppedCb_)
  : Object(display_, "ext_image_copy_capture_manager_v1",
           &ext_image_copy_capture_manager_v1_interface),
    manager(nullptr), display(display_), source(source_),
    session(nullptr), pointer(pointer_), cursorSession(nullptr),
    bufferEventCb(bufferEventCb_),
    pickShmFormatCb(pickShmFormatCb_),
    cursorImageCb(cursorImageCb_),
    cursorPosCb(cursorPosCb_),
    stoppedCb(stoppedCb_),
    active(true)
{
  manager = (ext_image_copy_capture_manager_v1*)boundObject;
}

ImageCopyCaptureManager::~ImageCopyCaptureManager()
{
  delete cursorSession;
  delete session;
  if (manager)
    ext_image_copy_capture_manager_v1_destroy(manager);
}

void ImageCopyCaptureManager::stopped()
{
  active = false;
  stoppedCb();
}

void ImageCopyCaptureManager::createSession()
{
  ext_image_copy_capture_session_v1* sessionHandle =
    ext_image_copy_capture_manager_v1_create_session(manager, source, 0);
  if (!sessionHandle)
    throw std::runtime_error(core::format("Unable to create image copy "
                                          "capture session"));

  std::function<void(const ImageCopyCaptureSession&)> frameReadyCb =
    [this](const ImageCopyCaptureSession& sessionRef) {
      bufferEventCb(sessionRef.getData(), sessionRef.getDamage(),
                    sessionRef.getFormat());
    };

  session = new ImageCopyCaptureSession(
    display, sessionHandle, frameReadyCb, pickShmFormatCb,
    std::bind(&ImageCopyCaptureManager::stopped, this));
}

void ImageCopyCaptureManager::createPointerCursorSession()
{
  ext_image_copy_capture_cursor_session_v1* cursorSessionHandle =
    ext_image_copy_capture_manager_v1_create_pointer_cursor_session(manager,
                                                                    source,
                                                                    pointer);
  if (!cursorSessionHandle)
    throw std::runtime_error(core::format("Unable to create image copy "
                                          "capture session"));

  std::function<void(const ImageCopyCaptureSession&,
                     const core::Point&)> cursorFrameCb =
    [this](const ImageCopyCaptureSession& sessionRef,
           const core::Point& hotspot) {
      uint8_t* data = sessionRef.getData();
      uint32_t width = sessionRef.getWidth();
      uint32_t height = sessionRef.getHeight();
      if (!data || !width || !height)
        return;
      uint32_t format = sessionRef.getFormat();
      cursorImageCb(width, height, hotspot, format, data);
    };

  cursorSession = new ImageCopyCaptureCursorSession(
    display, cursorSessionHandle, cursorFrameCb, cursorPosCb, pickShmFormatCb,
    std::bind(&ImageCopyCaptureManager::stopped, this));
}


ImageCopyCaptureSession::ImageCopyCaptureSession(
    Display* display_,
    ext_image_copy_capture_session_v1* session_,
    std::function<void(const ImageCopyCaptureSession&)> frameReadyCb_,
    std::function<bool(const std::vector<uint32_t>&, uint32_t*)>
      pickShmFormatCb_,
    std::function<void()> stoppedCb_)
  : display(display_), frameReadyCb(frameReadyCb_),
    pickShmFormatCb(pickShmFormatCb_), session(session_),
    frame(nullptr), shm(nullptr), pool(nullptr), buffer(nullptr),
    width(0), height(0), format(0), hasSize(false),
    constraintsReady(false), failed(false),
    stoppedCb(stoppedCb_)
{
  ext_image_copy_capture_session_v1_add_listener(session, &listener, this);
}

ImageCopyCaptureSession::~ImageCopyCaptureSession()
{
  if (frame)
    ext_image_copy_capture_frame_v1_destroy(frame);
  if (session)
    ext_image_copy_capture_session_v1_destroy(session);
  if (buffer)
    wl_buffer_destroy(buffer);
  delete pool;
  delete shm;
}

void ImageCopyCaptureSession::handleBufferSize(uint32_t width_,
                                               uint32_t height_)
{
  if (hasSize && (width_ != width || height_ != height)) {
    vlog.debug("Detected resize, destroying frame");
    if (frame) {
      ext_image_copy_capture_frame_v1_destroy(frame);
      frame = nullptr;
    }
    if (buffer) {
      wl_buffer_destroy(buffer);
      buffer = nullptr;
    }
    delete pool;
    pool = nullptr;
    constraintsReady = false;
    formats.clear();
    damage.clear();
  }

  width = width_;
  height = height_;
  hasSize = true;
}

void ImageCopyCaptureSession::handleShmFormat(uint32_t format_)
{
  formats.push_back(format_);
}

void ImageCopyCaptureSession::handleDmabufDevice(wl_array* /* device */)
{
}

void ImageCopyCaptureSession::handleDmabufFormat(uint32_t /* format */,
                                                 wl_array* /* modifiers */)
{
}

void ImageCopyCaptureSession::handleDone()
{
  uint32_t shmFormat = 0;
  size_t size;

  if (!session) {
    vlog.error("Missing session for image copy capture");
    if (stoppedCb)
      stoppedCb();
    return;
  }
  if (!width || !height) {
    vlog.error("Missing buffer size for image copy capture");
    if (stoppedCb)
      stoppedCb();
    return;
  }
  if (formats.empty()) {
    vlog.error("Missing shm formats for image copy capture");
    if (stoppedCb)
      stoppedCb();
    return;
  }

  if (!pickShmFormatCb || !pickShmFormatCb(formats, &shmFormat)) {
    vlog.error("No supported shm format for image copy capture");
    if (stoppedCb)
      stoppedCb();
    return;
  }

  format = shmFormat;
  constraintsReady = true;

  if (!shm)
    shm = new wayland::Shm(display);

  size = width * height * 4;
  if (!initPool("w0vncserver-image-copy-shm", size)) {
    if (stoppedCb)
      stoppedCb();
    return;
  }

  if (!pool)
  {
    if (stoppedCb)
      stoppedCb();
    return;
  }

  if (buffer)
    wl_buffer_destroy(buffer);
  buffer = pool->createBuffer(0, width, height, width * 4, format);

  createFrame();
}

void ImageCopyCaptureSession::handleStopped()
{
  failed = true;
  if (frame) {
    ext_image_copy_capture_frame_v1_destroy(frame);
    frame = nullptr;
  }
  if (session) {
    ext_image_copy_capture_session_v1_destroy(session);
    session = nullptr;
  }
  if (stoppedCb)
    stoppedCb();
}

void ImageCopyCaptureSession::createFrame()
{
  if (!session || !buffer)
    return;
  if (frame)
    return;

  frame = ext_image_copy_capture_session_v1_create_frame(session);
  if (!frame)
    return;

  ext_image_copy_capture_frame_v1_add_listener(frame, &frameListener, this);
  damage.clear();
  ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
  ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, width, height);
  ext_image_copy_capture_frame_v1_capture(frame);
}

bool ImageCopyCaptureSession::initPool(const char* name, size_t size)
{
  int fd;

  if (pool && pool->getSize() == size)
    return true;

  delete pool;
  pool = nullptr;

  fd = memfd_create(name, FD_CLOEXEC);
  if (fd < 0) {
    vlog.error("Failed to allocate shm: %s", strerror(errno));
    return false;
  }

  if (ftruncate(fd, size) < 0) {
    vlog.error("Failed to truncate shm: %s", strerror(errno));
    close(fd);
    return false;
  }

  pool = new wayland::ShmPool(shm, fd, size);
  close(fd);
  return true;
}

void ImageCopyCaptureSession::handleFrameTransform(uint32_t /* transform */)
{
}

void ImageCopyCaptureSession::handleFrameDamage(int32_t x, int32_t y,
                                                int32_t width_,
                                                int32_t height_)
{
  core::Point tl{static_cast<int>(x), static_cast<int>(y)};
  core::Point br{static_cast<int>(x + width_),
                 static_cast<int>(y + height_)};
  damage.assign_union({{tl, br}});
}

void ImageCopyCaptureSession::handleFramePresentationTime(uint32_t /* tvSecHi */,
                                                          uint32_t /* tvSecLo */,
                                                          uint32_t /* tvNsec */)
{
}

void ImageCopyCaptureSession::handleFrameReady()
{
  if (!constraintsReady || !pool)
    return;

  if (frameReadyCb)
    frameReadyCb(*this);

  if (frame) {
    ext_image_copy_capture_frame_v1_destroy(frame);
    frame = nullptr;
  }
  damage.clear();
  createFrame();
}

void ImageCopyCaptureSession::handleFrameFailed(uint32_t /* reason */)
{
  failed = true;
  if (frame) {
    ext_image_copy_capture_frame_v1_destroy(frame);
    frame = nullptr;
  }
}

uint8_t* ImageCopyCaptureSession::getData() const
{
  if (!pool)
    return nullptr;
  return pool->getData();
}

uint32_t ImageCopyCaptureSession::getWidth() const
{
  return width;
}

uint32_t ImageCopyCaptureSession::getHeight() const
{
  return height;
}

uint32_t ImageCopyCaptureSession::getFormat() const
{
  return format;
}

const core::Region& ImageCopyCaptureSession::getDamage() const
{
  return damage;
}

ImageCopyCaptureCursorSession::ImageCopyCaptureCursorSession(
    Display* display_,
    ext_image_copy_capture_cursor_session_v1* session_,
    std::function<void(const ImageCopyCaptureSession&,
                       const core::Point&)> cursorFrameCb_,
    std::function<void(const core::Point&)> cursorPosCb_,
    std::function<bool(const std::vector<uint32_t>&, uint32_t*)>
      pickShmFormatCb_,
    std::function<void()> stoppedCb_)
  : cursorFrameCb(cursorFrameCb_),
    cursorPosCb(cursorPosCb_),
    pickShmFormatCb(pickShmFormatCb_),
    session(session_), captureSession(nullptr),
    cursorHotspot({0, 0})
{
  ext_image_copy_capture_cursor_session_v1_add_listener(session,
                                                        &listener,
                                                        this);
  ext_image_copy_capture_session_v1* captureSessionHandle =
    ext_image_copy_capture_cursor_session_v1_get_capture_session(session);
  
  if (captureSessionHandle) {
    std::function<void(const ImageCopyCaptureSession&)> frameReadyCb =
      [this](const ImageCopyCaptureSession& captureSessionRef) {
        cursorFrameCb(captureSessionRef, cursorHotspot);
      };
    captureSession = new ImageCopyCaptureSession(display_,
                                                 captureSessionHandle,
                                                 frameReadyCb,
                                                 pickShmFormatCb,
                                                 stoppedCb_);
  }
}

ImageCopyCaptureCursorSession::~ImageCopyCaptureCursorSession()
{
  delete captureSession;
  if (session)
    ext_image_copy_capture_cursor_session_v1_destroy(session);
}

void ImageCopyCaptureCursorSession::handleEnter()
{
}

void ImageCopyCaptureCursorSession::handleLeave()
{
}

void ImageCopyCaptureCursorSession::handlePosition(int32_t x, int32_t y)
{
  if (cursorPosCb)
    cursorPosCb({static_cast<int>(x), static_cast<int>(y)});
}

void ImageCopyCaptureCursorSession::handleHotspot(int32_t x, int32_t y)
{
  cursorHotspot = {static_cast<int>(x), static_cast<int>(y)};
}
