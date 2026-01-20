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

#include <ext-image-capture-source-v1.h>

#include <core/LogWriter.h>

#include "Display.h"
#include "Output.h"
#include "ImageCaptureSource.h"

using namespace wayland;

static core::LogWriter vlog("WaylandImageCaptureSource");

ImageCaptureSource::ImageCaptureSource(ext_image_capture_source_v1* source_)
  : source(source_)
{
}

ImageCaptureSource::~ImageCaptureSource()
{
  if (source)
    ext_image_capture_source_v1_destroy(source);
}

OutputImageCaptureSourceManager::OutputImageCaptureSourceManager(Display* display_)
  : Object(display_, "ext_output_image_capture_source_manager_v1",
           &ext_output_image_capture_source_manager_v1_interface),
    manager(nullptr),
    display(display_)
{
  manager = (ext_output_image_capture_source_manager_v1*)boundObject;
}

OutputImageCaptureSourceManager::~OutputImageCaptureSourceManager()
{
  if (manager)
    ext_output_image_capture_source_manager_v1_destroy(manager);
}

ImageCaptureSource* OutputImageCaptureSourceManager::createSource(
    Output* output)
{
  ext_image_capture_source_v1* source;

  if (!manager || !output)
    return nullptr;

  source = ext_output_image_capture_source_manager_v1_create_source(
    manager, output->getOutput());
  if (!source)
    return nullptr;

  return new ImageCaptureSource(source);
}

ForeignToplevelImageCaptureSourceManager::ForeignToplevelImageCaptureSourceManager(
    Display* display_)
  : Object(display_, "ext_foreign_toplevel_image_capture_source_manager_v1",
           &ext_foreign_toplevel_image_capture_source_manager_v1_interface),
    manager(nullptr),
    display(display_)
{
  manager = (ext_foreign_toplevel_image_capture_source_manager_v1*)boundObject;
}

ForeignToplevelImageCaptureSourceManager::~ForeignToplevelImageCaptureSourceManager()
{
  if (manager)
    ext_foreign_toplevel_image_capture_source_manager_v1_destroy(manager);
}

ImageCaptureSource* ForeignToplevelImageCaptureSourceManager::createSource(
    ext_foreign_toplevel_handle_v1* toplevel)
{
  ext_image_capture_source_v1* source;

  if (!manager || !toplevel)
    return nullptr;

  source = ext_foreign_toplevel_image_capture_source_manager_v1_create_source(
    manager, toplevel);
  if (!source)
    return nullptr;

  return new ImageCaptureSource(source);
}
