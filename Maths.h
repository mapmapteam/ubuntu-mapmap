/*
 * Math.h
 *
 * (c) 2014 Sofian Audry -- info(@)sofianaudry(.)com
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


#ifndef MATH_H_
#define MATH_H_

#include <QtGlobal>
#include <cmath>

/// Converts from degrees to radians.
inline qreal degreesToRadians(qreal degrees) { return degrees / 180.0f * M_PI; }

/// Converts from radians to degrees.
inline qreal radiansToDegrees(qreal radians) { return radians / M_PI * 180.0f; }

/// Wrap value around ie. wrapAround(-1, 3) ==> 2
inline int wrapAround(int index, int max) { return (index + max) % max; }

/// Square of x.
inline qreal sq(qreal x) { return x*x; }

/**
 * Returns the squared euclidian distance between two points. This can be used rather than
 * dist() to compute the distance faster (prevents the sqrt() computation).
 */
inline qreal distSq(const QPointF& p1, const QPointF& p2) {
  return sq(p1.x() - p2.x()) + sq(p1.y() - p2.y());
}

/// Euclidian distance between two points.
inline qreal dist(const QPointF& p1, const QPointF& p2) {
  return sqrt( distSq(p1, p2) );
}

/// Returns true iff point #p1# is within distance #radius# of #p2# (using euclidian distance).
inline bool distIsInside(const QPointF& p1, const QPointF& p2, qreal radius) {
  return distSq(p1, p2) < sq(radius);
}

#endif /* MATH_H_ */
