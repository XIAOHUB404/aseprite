// Aseprite
// Copyright (C) 2015  David Capello
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/tools/symmetries.h"

#include "app/tools/stroke.h"
#include "app/tools/tool_loop.h"
#include "doc/brush.h"

namespace app {
namespace tools {

void HorizontalSymmetry::generateStrokes(const Stroke& mainStroke, Strokes& strokes,
                                         ToolLoop* loop)
{
  int adjust = (loop->getBrush()->bounds().w % 2);

  strokes.push_back(mainStroke);

  Stroke stroke2;
  for (const auto& pt : mainStroke)
    stroke2.addPoint(gfx::Point(m_x - (pt.x - m_x + adjust), pt.y));
  strokes.push_back(stroke2);
}

void VerticalSymmetry::generateStrokes(const Stroke& mainStroke, Strokes& strokes,
                                       ToolLoop* loop)
{
  int adjust = (loop->getBrush()->bounds().h % 2);

  strokes.push_back(mainStroke);

  Stroke stroke2;
  for (const auto& pt : mainStroke)
    stroke2.addPoint(gfx::Point(pt.x, m_y - (pt.y - m_y + adjust)));
  strokes.push_back(stroke2);
}

} // namespace tools
} // namespace app
