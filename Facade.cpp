/*
 * Facade.cpp
 *
 * (c) 2013 Sofian Audry -- info(@)sofianaudry(.)com
 * (c) 2013 Alexandre Quessy -- alexandre(@)quessy(.)net
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

#include "Facade.h"
#include <iostream>

Facade::Facade(MappingManager *manager, MainWindow *window) :
    _manager(manager),
    _window(window)
{
}

bool Facade::clearProject()
{
    std::cout << "TODO: Facade::clearProject" << std::endl;
}

bool Facade::createImagePaint(const char *paint_id, const char *uri)
{
    std::cout << "TODO: Facade::createImagePaint" << std::endl;
}

bool Facade::updateImagePaintUri(const char *paint_id, const char *uri)
{
    std::cout << "TODO: Facade::updateImagePaintUri" << std::endl;
}

bool Facade::createMeshTextureMapping(const char *mapping_id, const char *paint_id,
    int n_rows, int n_cols,
    const QList<QPointF> &src, const QList<QPointF> &dst)
{
    std::cout << "TODO: Facade::createMeshTextureMapping" << std::endl;
}

bool Facade::createTriangleTextureMapping(const char *mapping_id, const char *paint_id,
    const QList<QPointF> &src, const QList<QPointF> &dst)
{
    std::cout << "TODO: Facade::createTriangleTextureMapping" << std::endl;
}
