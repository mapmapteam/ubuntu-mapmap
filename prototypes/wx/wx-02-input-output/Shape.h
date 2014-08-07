/*
 * Shape.h
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


#ifndef SHAPE_H_
#define SHAPE_H_

#include <vector>
#include <SOIL/SOIL.h>

struct Point
{
  double x;
  double y;
  Point(double x_, double y_) : x(x_), y(y_) {}
};

class Shape
{
public:
  std::vector<Point> vertices;
  Shape() {}
  Shape(std::vector<Point> vertices_) : vertices(vertices_) {}
  virtual ~Shape() {}

  virtual void build() {}

  const Point& getVertex(int i) { return vertices[i]; }
  void setVertex(int i, Point v) { vertices[i] = v; }
  void setVertex(int i, double x, double y)
  {
    vertices[i].x = x;
    vertices[i].y = y;
  }
};

class Quad : public Shape
{
public:
  Quad() {}
  Quad(Point p1, Point p2, Point p3, Point p4) {
    vertices.push_back(p1);
    vertices.push_back(p2);
    vertices.push_back(p3);
    vertices.push_back(p4);
  }
  virtual ~Quad() {}
};

#endif /* SHAPE_H_ */
