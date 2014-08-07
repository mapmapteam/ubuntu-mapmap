/*
 * DestinationGLCanvas.h
 *
 * (c) 2013 Sofian Audry -- info(@)sofianaudry(.)com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DESTINATIONGLCANVAS_H_
#define DESTINATIONGLCANVAS_H_

#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <stdlib.h>
#include <stdio.h>
#include <SOIL/SOIL.h>

#include "MapperGLCanvas.h"

class DestinationGLCanvas: public MapperGLCanvas {
public:
  DestinationGLCanvas(wxFrame* parent);

  virtual Quad& getQuad() {
    std::tr1::shared_ptr<TextureMapping> textureMapping = std::tr1::static_pointer_cast<TextureMapping>(Common::currentMapping);
    wxASSERT(textureMapping != NULL);

    std::tr1::shared_ptr<Quad> quad = std::tr1::static_pointer_cast<Quad>(Common::currentMapping->getShape());
    wxASSERT(quad != NULL);

    return (*quad);
  }

private:
  virtual void doRender();
};

#endif /* DESTINATIONGLCANVAS_H_ */
