#include "io2d.h"
#include "xio2dhelpers.h"
#include "xcairoenumhelpers.h"
#include <algorithm>
#include <limits>

using namespace std;
using namespace std::experimental::io2d;

namespace {
	const double _Pi = 3.1415926535897932384626433832795;
}

path_factory::path_factory(path_factory&& other) noexcept
	: _Data()
	, _Has_current_point()
	, _Current_point()
	, _Extents_pt0()
	, _Extents_pt1()
	, _Transform_matrix()
	, _Origin() {
	_Data = move(other._Data);
	_Has_current_point = move(other._Has_current_point);
	_Current_point = move(other._Current_point);
	_Last_move_to_point = move(other._Last_move_to_point);
	_Extents_pt0 = move(other._Extents_pt0);
	_Extents_pt1 = move(other._Extents_pt1);
	_Transform_matrix = move(other._Transform_matrix);
	_Origin = move(other._Origin);
}

path_factory& path_factory::operator=(path_factory&& other) noexcept {
	if (this != &other) {
		_Data = move(other._Data);
		_Has_current_point = move(other._Has_current_point);
		_Current_point = move(other._Current_point);
		_Last_move_to_point = move(other._Last_move_to_point);
		_Extents_pt0 = move(other._Extents_pt0);
		_Extents_pt1 = move(other._Extents_pt1);
		_Transform_matrix = move(other._Transform_matrix);
		_Origin = move(other._Origin);
	}
	return *this;
}

void path_factory::append(const path& p) {
	const auto& data = p.data_ref();
	for (const auto& item : data) {
		_Data.push_back(item);
	}

	_Has_current_point = p._Has_current_point;
	_Current_point = p._Current_point;
	_Last_move_to_point = p._Last_move_to_point;
}

void path_factory::append(const path_factory& p) {
	for (const auto& item : p._Data) {
		_Data.push_back(item);
	}
	_Has_current_point = p._Has_current_point;
	_Current_point = p._Current_point;
	_Last_move_to_point = p._Last_move_to_point;
}

void path_factory::append(const vector<path_data_item>& p) {
	for (const auto& item : p) {
		_Data.push_back(item);
	}
}

bool path_factory::has_current_point() const {
	return _Has_current_point;
}

point path_factory::current_point() const {
	if (!_Has_current_point) {
		_Throw_if_failed_cairo_status_t(CAIRO_STATUS_NO_CURRENT_POINT);
	}
	return _Current_point;
}

void path_factory::new_sub_path() {
	_Data.emplace_back(experimental::io2d::new_sub_path());
	_Has_current_point = false;
}

void path_factory::close_path() {
	if (_Has_current_point) {
		_Data.emplace_back(experimental::io2d::close_path());
		_Current_point = _Last_move_to_point;
	}
}

point _Rotate_point(const point& pt, double angle, bool clockwise = true);
point _Rotate_point(const point& pt, double angle, bool clockwise) {
	if (clockwise) {
		return{ pt.x() * cos(angle) + pt.y() * sin(angle), -(pt.x() * -(sin(angle)) + pt.y() * cos(angle)) };
	}
	else {
		return{ pt.x() * cos(angle) + pt.y() * sin(angle), pt.x() * -(sin(angle)) + pt.y() * cos(angle) };
	}
}

vector<path_data_item> _Get_arc_as_beziers(const point& center, double radius, double angle1, double angle2, bool arcNegative, bool hasCurrentPoint, const point& currentPoint, const point& origin, const matrix_2d& matrix) {
	if (arcNegative) {
		while (angle2 > angle1) {
			angle2 -= _Pi * 2.0;
		}
	}
	else {
		while (angle2 < angle1) {
			angle2 += _Pi * 2.0;
		}
	}
	point pt0, pt1, pt2, pt3;
	int bezierCount = 1;
	double theta;
	if (arcNegative) {
		theta = angle1 - angle2;
	}
	else {
		theta = angle2 - angle1;
	}
	double phi;

	{
		// See: DeVeneza, Richard A., "How to determine the control points of a B�zier curve that approximates a small circular arc" (Nov. 2004) [ http://www.tinaja.com/glib/bezcirc2.pdf ].

		while (theta >= (_Pi / 2.0)) {
			theta /= 2.0;
			bezierCount += bezierCount;
		}

		phi = theta / 2.0;

		auto cosinePhi = cos(phi);
		auto sinePhi = sin(phi);

		pt0.x(cosinePhi);
		pt0.y(-sinePhi);

		pt3.x(pt0.x());
		pt3.y(-pt0.y());

		pt1.x((4.0 - cosinePhi) / 3.0);
		pt1.y(-(((1.0 - cosinePhi) * (3.0 - cosinePhi)) / (3.0 * sinePhi)));

		pt2.x(pt1.x());
		pt2.y(-pt1.y());

	}

	{
		if (!arcNegative) {
			phi = -phi;
		}
		// Rotate all points to start with a zero angle.
		pt0 = _Rotate_point(pt0, phi);
		pt1 = _Rotate_point(pt1, phi);
		pt2 = _Rotate_point(pt2, phi);
		pt3 = _Rotate_point(pt3, phi);

		if (arcNegative) {
			auto tempPt = pt3;
			pt3 = pt0;
			pt0 = tempPt;
			tempPt = pt2;
			pt2 = pt1;
			pt1 = tempPt;
		}
	}

	auto currentTheta = angle1;
	path_factory pb;
	pb.origin(origin);
	pb.transform_matrix(matrix);

	const auto startPoint = center + _Rotate_point({ pt0.x() * radius, pt0.y() * radius }, currentTheta);
	if (hasCurrentPoint) {
		pb.move_to(currentPoint);
		pb.line_to(startPoint);
	}
	else {
		pb.move_to(startPoint);
	}

	// We start at the point derived from angle1 and continue adding beziers until the count reaches zero.
	// The point we have is already rotated by half of theta.
	for (; bezierCount > 0; bezierCount--) {
		pb.curve_to(
			center + _Rotate_point({ pt1.x() * radius, pt1.y() * radius }, currentTheta),
			center + _Rotate_point({ pt2.x() * radius, pt2.y() * radius }, currentTheta),
			center + _Rotate_point({ pt3.x() * radius, pt3.y() * radius }, currentTheta)
			);

		if (arcNegative) {
			currentTheta -= theta;
		}
		else {
			currentTheta += theta;
		}
	}
	return pb.data();
}

void path_factory::_Set_current_point_and_last_move_to_point_for_arc(const vector<path_data_item>& data) {
	if (data.size() > 0) {
		const auto& lastItem = *data.crbegin();
		if (lastItem.type() == path_data_type::curve_to) {
			_Has_current_point = true;
			_Current_point = dynamic_cast<std::experimental::io2d::curve_to*>(lastItem.get().get())->end_point();
		}
		else if (lastItem.type() == path_data_type::line_to) {
			_Has_current_point = true;
			_Current_point = dynamic_cast<std::experimental::io2d::line_to*>(lastItem.get().get())->to();
		}
		else if (lastItem.type() == path_data_type::move_to) {
			_Has_current_point = true;
			_Current_point = dynamic_cast<std::experimental::io2d::move_to*>(lastItem.get().get())->to();
			_Last_move_to_point = _Current_point;
		}
		else {
			assert("_Get_arc_as_beziers returned unexpected path_data value." && false);
		}
	}
}

void path_factory::arc(const point& center, double radius, double angle1, double angle2) {
	_Data.emplace_back(std::experimental::io2d::arc(center, radius, angle1, angle2));
	// Update the current point.
	_Set_current_point_and_last_move_to_point_for_arc(_Get_arc_as_beziers(center, radius, angle1, angle2, false, _Has_current_point, _Current_point));
}

void path_factory::arc_negative(const point& center, double radius, double angle1, double angle2) {
	_Data.emplace_back(std::experimental::io2d::arc_negative(center, radius, angle1, angle2));
	// Update the current point.
	_Set_current_point_and_last_move_to_point_for_arc(_Get_arc_as_beziers(center, radius, angle1, angle2, true, _Has_current_point, _Current_point));
}

void path_factory::curve_to(const point& pt0, const point& pt1, const point& pt2) {
	if (!_Has_current_point) {
		move_to(pt0);
	}
	_Data.emplace_back(std::experimental::io2d::curve_to(pt0, pt1, pt2));
	_Has_current_point = true;
	_Current_point = pt2;
}

void path_factory::line_to(const point& pt) {
	if (!_Has_current_point) {
		move_to(pt);
		return;
	}
	_Data.emplace_back(std::experimental::io2d::line_to(pt));
	_Has_current_point = true;
	_Current_point = pt;
}

void path_factory::move_to(const point& pt) {
	_Data.emplace_back(std::experimental::io2d::move_to(pt));
	_Has_current_point = true;
	_Current_point = pt;
}

void path_factory::rect(const experimental::io2d::rectangle& r) {
	move_to({ r.x(), r.y() });
	rel_line_to({ r.width(), 0.0 });
	rel_line_to({ 0.0, r.height() });
	rel_line_to({ -r.width(), 0.0 });
	close_path();
}

void path_factory::rel_curve_to(const point& dpt0, const point& dpt1, const point& dpt2) {
	if (!_Has_current_point) {
		_Throw_if_failed_cairo_status_t(CAIRO_STATUS_NO_CURRENT_POINT);
	}
	_Data.emplace_back(std::experimental::io2d::rel_curve_to(dpt0, dpt1, dpt2));
	_Has_current_point = true;
	_Current_point = _Current_point + dpt2;
}

void path_factory::rel_line_to(const point& dpt) {
	if (!_Has_current_point) {
		_Throw_if_failed_cairo_status_t(CAIRO_STATUS_NO_CURRENT_POINT);
	}
	_Data.emplace_back(std::experimental::io2d::rel_line_to(dpt));
	_Has_current_point = true;
	_Current_point = _Current_point + dpt;
}

void path_factory::rel_move_to(const point& dpt) {
	if (!_Has_current_point) {
		_Throw_if_failed_cairo_status_t(CAIRO_STATUS_NO_CURRENT_POINT);
	}
	_Data.emplace_back(std::experimental::io2d::rel_move_to(dpt));
	_Has_current_point = true;
	_Current_point = _Current_point + dpt;
}

void path_factory::transform_matrix(const matrix_2d& m) {
	_Data.emplace_back(std::experimental::io2d::change_matrix(m));
	_Transform_matrix = m;
}

matrix_2d path_factory::transform_matrix() const {
	return _Transform_matrix;
}

void path_factory::origin(const point& pt) {
	_Data.emplace_back(std::experimental::io2d::change_origin(pt));
	_Origin = pt;
}

point path_factory::origin() const {
	return _Origin;
}

vector<path_data_item> path_factory::data() const {
	vector<path_data_item> result;
	for (const auto& item : _Data) {
		result.push_back(item);
	}
	return result;
}

path_data_item path_factory::data_item(unsigned int index) const {
	if (_Data.size() <= index) {
		_Throw_if_failed_cairo_status_t(CAIRO_STATUS_INVALID_INDEX);
	}
	return _Data[index];
}

const vector<path_data_item>& path_factory::data_ref() const {
	return _Data;
}

double _Curve_value_for_t(double a, double b, double c, double d, double t);
double _Curve_value_for_t(double a, double b, double c, double d, double t) {
	return pow(1.0 - t, 3.0) * a + 3.0 * pow(1.0 - t, 2.0) * t * b + 3.0 * (1.0 - t) * pow(t, 2.0) * c + pow(t, 3.0) * d;
}

inline point _Cubic_bezier_derivative_for_t(const point& pt0, const point& pt1, const point& pt2, const point& pt3, double t);
inline point _Cubic_bezier_derivative_for_t(const point& pt0, const point& pt1, const point& pt2, const point& pt3, double t) {
	return point{ 3.0 * pow(1.0 - t, 2.0) * (pt1 - pt0).x(), 3.0 * pow(1.0 - t, 2.0) * (pt1 - pt0).y() } + point{ 6.0 * (1.0 - t) * t * (pt2 - pt1).x(), 6.0 * (1.0 - t) * t * (pt2 - pt1).y()} + point{3.0 * pow(t, 2.0) * (pt3 - pt2).x(), 3.0 * pow(t, 2.0) * (pt3 - pt2).y() };
}

inline bool _Same_sign(double lhs, double rhs);
inline bool _Same_sign(double lhs, double rhs) {
	return ((lhs < 0.0) && (rhs < 0.0)) || ((lhs > 0.0) && (rhs > 0.0));
}

double _Find_t_for_d_of_t_equal_zero(const point& pt0, const point& pt1, const point& pt2, const point& pt3, double t0, double t2, const bool findX, const double epsilon = numeric_limits<double>::epsilon());
double _Find_t_for_d_of_t_equal_zero(const point& pt0, const point& pt1, const point& pt2, const point& pt3, double t0, double t2, const bool findX, const double epsilon) {
	// Validate that t0 is the low value, t2 is the high value, t0 is not equal to t2, and that both are in the range [0.0, 1.0].
	assert(t0 >= 0.0 && t0 < t2 && t2 <= 1.0);
	// Find the midpoint.
	double t1 = (t2 - t0) / 2.0 + t0;

	double t1Previous = -1.0;
	// Short-circuit and return current t0 if t0 and t1 are equal or t1 and t1Previous are equal to avoid infinite looping.
	if (_Almost_equal_relative(t0, t1, epsilon) || _Almost_equal_relative(t1, t1Previous, epsilon)) {
		return t0;
	}
	auto dt0 = _Cubic_bezier_derivative_for_t(pt0, pt1, pt2, pt3, t0);
	auto dt2 = _Cubic_bezier_derivative_for_t(pt0, pt1, pt2, pt3, t2);

	if (findX) {
		assert(!_Same_sign(dt0.x(), dt2.x()));
	}
	else {
		assert(!_Same_sign(dt0.y(), dt2.y()));
	}

	auto dt1 = +_Cubic_bezier_derivative_for_t(pt0, pt1, pt2, pt3, t1);

	// If t0 and t1 are equal or t1 and t1Previous are equal, we can no longer get a meaningful t1 value so the value of t0 will have to be accepted as close enough.
	while (!_Almost_equal_relative(t0, t1, epsilon) && !_Almost_equal_relative(t1, t1Previous, epsilon)) {
		if (findX) {
			if (_Almost_equal_relative(dt1.x(), 0.0, epsilon)) {
				return t1;
			}
			if (_Same_sign(dt0.x(), dt1.x())) {
				// Since t0 and t2 are different signs and t0 and t1 are the same sign, we know our value lies between t1 and t2 so set t0 = t1 and calculate the new t1.
				t0 = t1;
				t1Previous = t1;
				t1 = (t2 - t0) / 2.0 + t0;
			}
			else {
				assert(_Same_sign(dt1.x(), dt2.x()));
				// Since t0 and t2 are different signs and t1 and t2 are the same sign, we know our value lies between t0 and t1 so set t2 = t1 and calculate the new t1.
				t2 = t1;
				t1Previous = t1;
				t1 = (t2 - t0) / 2.0 + t0;
			}
		}
		else {
			if (_Almost_equal_relative(dt1.y(), 0.0, epsilon)) {
				return t1;
			}
			if (_Same_sign(dt0.y(), dt1.y())) {
				// Since t0 and t2 are different signs and t0 and t1 are the same sign, we know our value lies between t1 and t2 so set t0 = t1 and calculate the new t1.
				t0 = t1;
				t1Previous = t1;
				t1 = (t2 - t0) / 2.0 + t0;
			}
			else {
				assert(_Same_sign(dt1.y(), dt2.y()));
				// Since t0 and t2 are different signs and t1 and t2 are the same sign, we know our value lies between t0 and t1 so set t2 = t1 and calculate the new t1.
				t2 = t1;
				t1Previous = t1;
				t1 = (t2 - t0) / 2.0 + t0;
			}
		}
	}
	return t1;
}

void _Curve_to_extents(const point& pt0, const point& pt1, const point& pt2, const point& pt3, point& extents0, point& extents1);
void _Curve_to_extents(const point& pt0, const point& pt1, const point& pt2, const point& pt3, point& extents0, point& extents1) {
	// We know at a minimum that the extents are the two knots, pt0 and pt3. The only question is whether the extents go beyond those two points.
	extents0.x(min(pt0.x(), pt3.x()));
	extents0.y(min(pt0.y(), pt3.y()));
	extents1.x(max(pt0.x(), pt3.x()));
	extents1.y(max(pt0.y(), pt3.y()));

	// Find X's and Ys (between 0 and 2).
	int numPoints = 0;
	int numXs = 0;
	int numYs = 0;

	const auto dt0 = _Cubic_bezier_derivative_for_t(pt0, pt1, pt2, pt3, 0.0);
	const auto dt1 = _Cubic_bezier_derivative_for_t(pt0, pt1, pt2, pt3, 0.5);
	const auto dt2 = _Cubic_bezier_derivative_for_t(pt0, pt1, pt2, pt3, 1.0);
	bool foundLowX = false;
	bool foundHighX = false;
	bool foundLowY = false;
	bool foundHighY = false;

	const double epsilon = numeric_limits<double>::epsilon();

	// X values
	if (_Almost_equal_relative(dt0.x(), 0.0, epsilon)) {
		assert(numXs == 0);
		// First knot is critical. We already assigned it so we're done with that.
		foundLowX = true;
		numPoints++;
		numXs++;
		if (_Same_sign(dt1.x(), dt2.x())) {
			// No second critical point so the second knot is the other extent and we already assigned it so we're done with that.
			foundHighX = true;
			numPoints++;
			numXs++;
		}
		else {
			auto t = _Find_t_for_d_of_t_equal_zero(pt0, pt1, pt2, pt3, 0.5, 1.0, true);
			auto xval = _Curve_value_for_t(pt0.x(), pt1.x(), pt2.x(), pt3.x(), t);
			// We do this min/max assignment because critical points just signal a change in curve direction, not that the critical point is actually a min/max point.
			extents0.x(min(extents0.x(), xval));
			extents1.x(max(extents1.x(), xval));
			foundHighX = true;
			numPoints++;
			numXs++;
		}
	}
	if (_Almost_equal_relative(dt1.x(), 0.0, epsilon)) {
		assert(numXs == 0);
		// Center is only critical.
		auto cxval = _Curve_value_for_t(pt0.x(), pt1.x(), pt2.x(), pt3.x(), 0.5);
		// Arbitrarily use pt3.x() rather than pt0.x(); they are the same value regardless.
		assert(_Almost_equal_relative(pt0.x(), pt3.x(), epsilon));
		extents0.x(min(cxval, pt3.x()));
		extents1.x(max(cxval, pt3.x()));
		numPoints += 2;
		numXs += 2;
	}
	if (_Almost_equal_relative(dt2.x(), 0.0, epsilon)) {
		assert(numXs == 0);
		numPoints++;
		numXs++;
		foundHighX = true;
		if (_Same_sign(dt0.x(), dt1.x())) {
			// No second critical point so the first knot is the other extent and we already assigned it so we're done with that.
			foundLowX = true;
			numPoints++;
			numXs++;
		}
		else {
			auto t = _Find_t_for_d_of_t_equal_zero(pt0, pt1, pt2, pt3, 0.0, 0.5, true);
			auto xval = _Curve_value_for_t(pt0.y(), pt1.y(), pt2.y(), pt3.y(), t);
			// We do this min/max assignment because critical points just signal a change in curve direction, not that the critical point is actually a min/max point.
			extents0.x(min(extents0.x(), xval));
			extents1.x(max(extents1.x(), xval));
			foundLowX = true;
			numPoints++;
			numXs++;
		}
	}
	if (numXs == 0) {
		if (_Same_sign(dt0.x(), dt1.x()) && _Same_sign(dt1.x(), dt2.x())) {
			// No critical points on X: use ends.
			extents0.x(min(pt0.x(), pt3.x()));
			extents1.x(max(pt0.x(), pt3.x()));
			foundLowX = true;
			foundHighX = true;
			numPoints += 2;
			numXs += 2;
		}
	}
	if (!foundLowX) {
		if (_Same_sign(dt0.x(), dt1.x())) {
			// There is no critical point between dt0.x() and dt1.x() so the lowX point is pt0.x(), which we already assigned.
			foundLowX = true;
			numPoints++;
			numXs++;
		}
		else {
			auto t = _Find_t_for_d_of_t_equal_zero(pt0, pt1, pt2, pt3, 0.0, 0.5, true);
			auto xval = _Curve_value_for_t(pt0.x(), pt1.x(), pt2.x(), pt3.x(), t);
			// We do this min/max assignment because critical points just signal a change in curve direction, not that the critical point is actually a min/max point.
			extents0.x(min(extents0.x(), xval));
			extents1.x(max(extents1.x(), xval));
			foundLowX = true;
			numPoints++;
			numXs++;
		}
	}
	if (!foundHighX) {
		if (_Same_sign(dt1.x(), dt2.x())) {
			// There is no critical point between dt1.x and dt2.x so the highX point is pt3.x, which we already assigned.
			foundHighX = true;
			numPoints++;
			numXs++;
		}
		else {
			auto t = _Find_t_for_d_of_t_equal_zero(pt0, pt1, pt2, pt3, 0.5, 1.0, true);
			auto xval = _Curve_value_for_t(pt0.x(), pt1.x(), pt2.x(), pt3.x(), t);
			// We do this min/max assignment because critical points just signal a change in curve direction, not that the critical point is actually a min/max point.
			extents0.x(min(extents0.x(), xval));
			extents1.x(max(extents1.x(), xval));
			foundHighX = true;
			numPoints++;
			numXs++;
		}
	}

	// Y values
	if (_Almost_equal_relative(dt0.y(), 0.0, epsilon)) {
		assert(numYs == 0);
		// First knot is critical. We already assigned it so we're done with that.
		foundLowY = true;
		numPoints++;
		numYs++;
		if (_Same_sign(dt1.y(), dt2.y())) {
			// No second critical point so the second knot is the other extent and we already assigned it so we're done with that.
			foundHighY = true;
			numPoints++;
			numYs++;
		}
		else {
			auto t = _Find_t_for_d_of_t_equal_zero(pt0, pt1, pt2, pt3, 0.5, 1.0, true);
			auto yval = _Curve_value_for_t(pt0.y(), pt1.y(), pt2.y(), pt3.y(), t);
			// We do this min/max assignment because critical points just signal a change in curve direction, not that the critical point is actually a min/max point.
			extents0.y(min(extents0.y(), yval));
			extents1.y(max(extents1.y(), yval));
			foundHighY = true;
			numPoints++;
			numYs++;
		}
	}
	if (_Almost_equal_relative(dt1.y(), 0.0, epsilon)) {
		assert(numYs == 0);
		// Center is only critical.
		auto cyval = _Curve_value_for_t(pt0.y(), pt1.y(), pt2.y(), pt3.y(), 0.5);
		// Arbitrarily use pt3.y() rather than pt0.y(); they are the same value regardless.
		assert(_Almost_equal_relative(pt0.y(), pt3.y(), epsilon));
		extents0.y(min(cyval, pt3.y()));
		extents1.y(max(cyval, pt3.y()));
		numPoints += 2;
		numYs += 2;
	}
	if (_Almost_equal_relative(dt2.y(), 0.0, epsilon)) {
		assert(numYs == 0);
		numPoints++;
		numYs++;
		foundHighY = true;
		if (_Same_sign(dt0.y(), dt1.y())) {
			// No second critical point so the first knot is the other extent and we already assigned it so we're done with that.
			foundLowY = true;
			numPoints++;
			numYs++;
		}
		else {
			auto t = _Find_t_for_d_of_t_equal_zero(pt0, pt1, pt2, pt3, 0.0, 0.5, true);
			auto yval = _Curve_value_for_t(pt0.y(), pt1.y(), pt2.y(), pt3.y(), t);
			// We do this min/max assignment because critical points just signal a change in curve direction, not that the critical point is actually a min/max point.
			extents0.y(min(extents0.y(), yval));
			extents1.y(max(extents1.y(), yval));
			foundLowY = true;
			numPoints++;
			numYs++;
		}
	}
	if (numYs == 0) {
		if (_Same_sign(dt0.y(), dt1.y()) && _Same_sign(dt1.y(), dt2.y())) {
			// No critical points on Y: use ends.
			extents0.y(min(pt0.y(), pt3.y()));
			extents1.y(max(pt0.y(), pt3.y()));
			foundLowY = true;
			foundHighY = true;
			numPoints += 2;
			numYs += 2;
		}
	}
	if (!foundLowY) {
		if (_Same_sign(dt0.y(), dt1.y())) {
			// There is no critical point between dt0.y() and dt1.y() so the lowY point is pt0.y(), which we already assigned.
			foundLowY = true;
			numPoints++;
			numYs++;
		}
		else {
			auto t = _Find_t_for_d_of_t_equal_zero(pt0, pt1, pt2, pt3, 0.0, 0.5, false);
			auto yval = _Curve_value_for_t(pt0.y(), pt1.y(), pt2.y(), pt3.y(), t);
			extents0.y(min(extents0.y(), yval));
			extents1.y(max(extents1.y(), yval));
			foundLowY = true;
			numPoints++;
			numYs++;
		}
	}
	if (!foundHighY) {
		if (_Same_sign(dt1.y(), dt2.y())) {
			// There is no critical point between dt1.y() and dt2.y() so the lowY point is pt3.y(), which we already assigned.
			foundHighY = true;
			numPoints++;
			numYs++;
		}
		else {
			auto t = _Find_t_for_d_of_t_equal_zero(pt0, pt1, pt2, pt3, 0.5, 1.0, false);
			auto yval = _Curve_value_for_t(pt0.y(), pt1.y(), pt2.y(), pt3.y(), t);
			extents0.y(min(extents0.y(), yval));
			extents1.y(max(extents1.y(), yval));
			foundHighY = true;
			numPoints++;
			numYs++;
		}
	}
	assert(foundLowX && foundLowY && foundHighX && foundHighY && numPoints == 4 && numXs == 2 && numYs == 2);
}

rectangle path_factory::path_extents() const {
	point pt0{ };
	point pt1{ };

	matrix_2d currMatrix = matrix_2d::init_identity();
	point currOrigin{ };

	bool hasLastPoint = false;
	bool hasExtents = false;

	point lastPoint{ };

	// pt0 will hold min values; pt1 will hold max values.
	for (auto i = 0U; i < _Data.size(); i++) {
		const auto& item = _Data[i];
		auto type = item.type();
		switch (type)
		{
		case std::experimental::io2d::path_data_type::move_to:
			lastPoint = currMatrix.transform_point(item.get<experimental::io2d::move_to>().to() - currOrigin) + currOrigin;
			hasLastPoint = true;
			break;
		case std::experimental::io2d::path_data_type::line_to:
			if (hasLastPoint) {
				auto itemPt = currMatrix.transform_point(item.get<experimental::io2d::line_to>().to() - currOrigin) + currOrigin;
				if (!hasExtents) {
					hasExtents = true;
					pt0.x(min(lastPoint.x(), itemPt.x()));
					pt0.y(min(lastPoint.y(), itemPt.y()));
					pt1.x(max(lastPoint.x(), itemPt.x()));
					pt1.y(max(lastPoint.y(), itemPt.y()));
				}
				else {
					pt0.x(min(min(pt0.x(), lastPoint.x()), itemPt.x()));
					pt0.y(min(min(pt0.y(), lastPoint.y()), itemPt.y()));
					pt1.x(max(max(pt1.x(), lastPoint.x()), itemPt.x()));
					pt1.y(max(max(pt1.y(), lastPoint.y()), itemPt.y()));
				}
			}
			else {
				_Throw_if_failed_cairo_status_t(CAIRO_STATUS_NO_CURRENT_POINT);
			}
			break;
		case std::experimental::io2d::path_data_type::curve_to:
		{
			if (!hasLastPoint) {
				_Throw_if_failed_cairo_status_t(CAIRO_STATUS_NO_CURRENT_POINT);
			}
			point cte0{ };
			point cte1{ };
			auto dataItem = item.get<experimental::io2d::curve_to>();
			auto itemPt1 = currMatrix.transform_point(dataItem.control_point_1() - currOrigin) + currOrigin;
			auto itemPt2 = currMatrix.transform_point(dataItem.control_point_2() - currOrigin) + currOrigin;
			auto itemPt3 = currMatrix.transform_point(dataItem.end_point() - currOrigin) + currOrigin;
			_Curve_to_extents(lastPoint, itemPt1, itemPt2, itemPt3, cte0, cte1);
			if (!hasExtents) {
				hasExtents = true;
				pt0.x(min(cte0.x(), cte1.x()));
				pt0.y(min(cte0.y(), cte1.y()));
				pt1.x(max(cte0.x(), cte1.x()));
				pt1.y(max(cte0.y(), cte1.y()));
			}
			else {
				pt0.x(min(min(pt0.x(), cte0.x()), cte1.x()));
				pt0.y(min(min(pt0.y(), cte0.y()), cte1.y()));
				pt1.x(max(max(pt1.x(), cte0.x()), cte1.x()));
				pt1.y(max(max(pt1.y(), cte0.y()), cte1.y()));
			}
		}
		break;
		case std::experimental::io2d::path_data_type::new_sub_path:
			hasLastPoint = false;
			break;
		case std::experimental::io2d::path_data_type::close_path:
			hasLastPoint = false;
			break;
		case std::experimental::io2d::path_data_type::rel_move_to:
			if (!hasLastPoint) {
				_Throw_if_failed_cairo_status_t(CAIRO_STATUS_INVALID_PATH_DATA);
			}
			lastPoint = currMatrix.transform_point((item.get<experimental::io2d::rel_move_to>().to() + lastPoint) - currOrigin) + currOrigin;
			hasLastPoint = true;
			break;
		case std::experimental::io2d::path_data_type::rel_line_to:
			if (hasLastPoint) {
				auto itemPt = currMatrix.transform_point((item.get<experimental::io2d::rel_line_to>().to() + lastPoint) - currOrigin) + currOrigin;
				if (!hasExtents) {
					hasExtents = true;
					pt0.x(min(lastPoint.x(), itemPt.x()));
					pt0.y(min(lastPoint.y(), itemPt.y()));
					pt1.x(max(lastPoint.x(), itemPt.x()));
					pt1.y(max(lastPoint.y(), itemPt.y()));
				}
				else {
					pt0.x(min(min(pt0.x(), lastPoint.x()), itemPt.x()));
					pt0.y(min(min(pt0.y(), lastPoint.y()), itemPt.y()));
					pt1.x(max(max(pt1.x(), lastPoint.x()), itemPt.x()));
					pt1.y(max(max(pt1.y(), lastPoint.y()), itemPt.y()));
				}
			}
			else {
				_Throw_if_failed_cairo_status_t(CAIRO_STATUS_INVALID_PATH_DATA);
			}
			break;
		case std::experimental::io2d::path_data_type::rel_curve_to:
		{
			if (!hasLastPoint) {
				_Throw_if_failed_cairo_status_t(CAIRO_STATUS_NO_CURRENT_POINT);
			}
			point cte0{ };
			point cte1{ };
			auto dataItem = item.get<experimental::io2d::rel_curve_to>();
			auto itemPt1 = currMatrix.transform_point((dataItem.control_point_1() + lastPoint) - currOrigin) + currOrigin;
			auto itemPt2 = currMatrix.transform_point((dataItem.control_point_2() + lastPoint) - currOrigin) + currOrigin;
			auto itemPt3 = currMatrix.transform_point((dataItem.end_point() + lastPoint) - currOrigin) + currOrigin;
			_Curve_to_extents(lastPoint, itemPt1, itemPt2, itemPt3, cte0, cte1);
			if (!hasExtents) {
				hasExtents = true;
				pt0.x(min(cte0.x(), cte1.x()));
				pt0.y(min(cte0.y(), cte1.y()));
				pt1.x(max(cte0.x(), cte1.x()));
				pt1.y(max(cte0.y(), cte1.y()));
			}
			else {
				pt0.x(min(min(pt0.x(), cte0.x()), cte1.x()));
				pt0.y(min(min(pt0.y(), cte0.y()), cte1.y()));
				pt1.x(max(max(pt1.x(), cte0.x()), cte1.x()));
				pt1.y(max(max(pt1.y(), cte0.y()), cte1.y()));
			}
		}
		break;
		case std::experimental::io2d::path_data_type::arc:
		{
			auto dataItem = item.get<experimental::io2d::arc>();
			auto data = _Get_arc_as_beziers(dataItem.center(), dataItem.radius(), dataItem.angle_1(), dataItem.angle_2(), false, hasLastPoint, lastPoint, currOrigin, currMatrix);
			for (const auto& arcItem : data) {
				switch (arcItem.type()) {
				case std::experimental::io2d::path_data_type::move_to:
					lastPoint = currMatrix.transform_point(arcItem.get<experimental::io2d::move_to>().to() - currOrigin) + currOrigin;
					hasLastPoint = true;
					break;
				case std::experimental::io2d::path_data_type::line_to:
					if (hasLastPoint) {
						auto itemPt = currMatrix.transform_point(arcItem.get<experimental::io2d::line_to>().to() - currOrigin) + currOrigin;
						if (!hasExtents) {
							hasExtents = true;
							pt0.x(min(lastPoint.x(), itemPt.x()));
							pt0.y(min(lastPoint.y(), itemPt.y()));
							pt1.x(max(lastPoint.x(), itemPt.x()));
							pt1.y(max(lastPoint.y(), itemPt.y()));
						}
						else {
							pt0.x(min(min(pt0.x(), lastPoint.x()), itemPt.x()));
							pt0.y(min(min(pt0.y(), lastPoint.y()), itemPt.y()));
							pt1.x(max(max(pt1.x(), lastPoint.x()), itemPt.x()));
							pt1.y(max(max(pt1.y(), lastPoint.y()), itemPt.y()));
						}
					}
					else {
						_Throw_if_failed_cairo_status_t(CAIRO_STATUS_INVALID_PATH_DATA);
					}
					break;
				case std::experimental::io2d::path_data_type::curve_to:
				{
					if (!hasLastPoint) {
						_Throw_if_failed_cairo_status_t(CAIRO_STATUS_NO_CURRENT_POINT);
					}
					point cte0{ };
					point cte1{ };
					auto arcDataItem = arcItem.get<experimental::io2d::curve_to>();
					auto itemPt1 = currMatrix.transform_point(arcDataItem.control_point_1() - currOrigin) + currOrigin;
					auto itemPt2 = currMatrix.transform_point(arcDataItem.control_point_2() - currOrigin) + currOrigin;
					auto itemPt3 = currMatrix.transform_point(arcDataItem.end_point() - currOrigin) + currOrigin;
					_Curve_to_extents(lastPoint, itemPt1, itemPt2, itemPt3, cte0, cte1);
					if (!hasExtents) {
						hasExtents = true;
						pt0.x(min(cte0.x(), cte1.x()));
						pt0.y(min(cte0.y(), cte1.y()));
						pt1.x(max(cte0.x(), cte1.x()));
						pt1.y(max(cte0.y(), cte1.y()));
					}
					else {
						pt0.x(min(min(pt0.x(), cte0.x()), cte1.x()));
						pt0.y(min(min(pt0.y(), cte0.y()), cte1.y()));
						pt1.x(max(max(pt1.x(), cte0.x()), cte1.x()));
						pt1.y(max(max(pt1.y(), cte0.y()), cte1.y()));
					}
				}
				break;
				case path_data_type::new_sub_path:
				{
					assert("Unexpected value path_data_type::new_sub_path." && false);
					throw runtime_error("Unexpected value path_data_type::new_sub_path.");
				}
				case path_data_type::close_path:
				{
					assert("Unexpected value path_data_type::close_path." && false);
					throw runtime_error("Unexpected value path_data_type::close_path.");
				}
				case path_data_type::rel_move_to:
				{
					assert("Unexpected value path_data_type::rel_move_to." && false);
					throw runtime_error("Unexpected value path_data_type::rel_move_to.");
				}
				case path_data_type::rel_line_to:
				{
					assert("Unexpected value path_data_type::rel_line_to." && false);
					throw runtime_error("Unexpected value path_data_type::rel_line_to.");
				}
				case path_data_type::rel_curve_to:
				{
					assert("Unexpected value path_data_type::rel_curve_to." && false);
					throw runtime_error("Unexpected value path_data_type::rel_curve_to.");
				}
				case path_data_type::arc:
				{
					assert("Unexpected value path_data_type::arc." && false);
					throw runtime_error("Unexpected value path_data_type::arc.");
				}
				case path_data_type::arc_negative:
				{
					assert("Unexpected value path_data_type::arc_negative." && false);
					throw runtime_error("Unexpected value path_data_type::arc_negative.");
				}
				case path_data_type::change_origin:
				{
					// Ignore, we're already dealing with this.
				}
				break;

				case path_data_type::change_matrix:
				{
					// Ignore, we're already dealing with this.
				}
				break;
				default:
					assert("Unexpected path_data_type in arc." && false);
					break;
				}
			}
		}
		break;
		case std::experimental::io2d::path_data_type::arc_negative:
		{
			auto dataItem = item.get<experimental::io2d::arc_negative>();
			auto data = _Get_arc_as_beziers(dataItem.center(), dataItem.radius(), dataItem.angle_1(), dataItem.angle_2(), false, hasLastPoint, lastPoint, currOrigin, currMatrix);
			for (const auto& arcItem : data) {
				switch (arcItem.type()) {
				case std::experimental::io2d::path_data_type::move_to:
					lastPoint = currMatrix.transform_point(arcItem.get<experimental::io2d::move_to>().to() - currOrigin) + currOrigin;
					hasLastPoint = true;
					break;
				case std::experimental::io2d::path_data_type::line_to:
					if (hasLastPoint) {
						auto itemPt = currMatrix.transform_point(arcItem.get<experimental::io2d::line_to>().to() - currOrigin) + currOrigin;
						if (!hasExtents) {
							hasExtents = true;
							pt0.x(min(lastPoint.x(), itemPt.x()));
							pt0.y(min(lastPoint.y(), itemPt.y()));
							pt1.x(max(lastPoint.x(), itemPt.x()));
							pt1.y(max(lastPoint.y(), itemPt.y()));
						}
						else {
							pt0.x(min(min(pt0.x(), lastPoint.x()), itemPt.x()));
							pt0.y(min(min(pt0.y(), lastPoint.y()), itemPt.y()));
							pt1.x(max(max(pt1.x(), lastPoint.x()), itemPt.x()));
							pt1.y(max(max(pt1.y(), lastPoint.y()), itemPt.y()));
						}
					}
					else {
						_Throw_if_failed_cairo_status_t(CAIRO_STATUS_INVALID_PATH_DATA);
					}
					break;
				case std::experimental::io2d::path_data_type::curve_to:
				{
					if (!hasLastPoint) {
						_Throw_if_failed_cairo_status_t(CAIRO_STATUS_NO_CURRENT_POINT);
					}
					point cte0{ };
					point cte1{ };
					auto arcDataItem = arcItem.get<experimental::io2d::curve_to>();
					auto itemPt1 = currMatrix.transform_point(arcDataItem.control_point_1() - currOrigin) + currOrigin;
					auto itemPt2 = currMatrix.transform_point(arcDataItem.control_point_2() - currOrigin) + currOrigin;
					auto itemPt3 = currMatrix.transform_point(arcDataItem.end_point() - currOrigin) + currOrigin;
					_Curve_to_extents(lastPoint, itemPt1, itemPt2, itemPt3, cte0, cte1);
					if (!hasExtents) {
						hasExtents = true;
						pt0.x(min(cte0.x(), cte1.x()));
						pt0.y(min(cte0.y(), cte1.y()));
						pt1.x(max(cte0.x(), cte1.x()));
						pt1.y(max(cte0.y(), cte1.y()));
					}
					else {
						pt0.x(min(min(pt0.x(), cte0.x()), cte1.x()));
						pt0.y(min(min(pt0.y(), cte0.y()), cte1.y()));
						pt1.x(max(max(pt1.x(), cte0.x()), cte1.x()));
						pt1.y(max(max(pt1.y(), cte0.y()), cte1.y()));
					}
				}
				break;
				case path_data_type::new_sub_path:
				{
					assert("Unexpected value path_data_type::new_sub_path." && false);
					throw runtime_error("Unexpected value path_data_type::new_sub_path.");
				}
				case path_data_type::close_path:
				{
					assert("Unexpected value path_data_type::close_path." && false);
					throw runtime_error("Unexpected value path_data_type::close_path.");
				}
				case path_data_type::rel_move_to:
				{
					assert("Unexpected value path_data_type::rel_move_to." && false);
					throw runtime_error("Unexpected value path_data_type::rel_move_to.");
				}
				case path_data_type::rel_line_to:
				{
					assert("Unexpected value path_data_type::rel_line_to." && false);
					throw runtime_error("Unexpected value path_data_type::rel_line_to.");
				}
				case path_data_type::rel_curve_to:
				{
					assert("Unexpected value path_data_type::rel_curve_to." && false);
					throw runtime_error("Unexpected value path_data_type::rel_curve_to.");
				}
				case path_data_type::arc:
				{
					assert("Unexpected value path_data_type::arc." && false);
					throw runtime_error("Unexpected value path_data_type::arc.");
				}
				case path_data_type::arc_negative:
				{
					assert("Unexpected value path_data_type::arc_negative." && false);
					throw runtime_error("Unexpected value path_data_type::arc_negative.");
				}
				case path_data_type::change_origin:
				{
					// Ignore, we're already dealing with this.
				}
				break;

				case path_data_type::change_matrix:
				{
					// Ignore, we're already dealing with this.
				}
				break;
				default:
					assert("Unexpected path_data_type in arc." && false);
					break;
				}
			}
		} break;
		case std::experimental::io2d::path_data_type::change_matrix:
		{
			currMatrix = item.get<experimental::io2d::change_matrix>().matrix();
		} break;
		case std::experimental::io2d::path_data_type::change_origin:
			currOrigin = item.get<experimental::io2d::change_origin>().origin();
			break;
		default:
			assert("Unknown path_data_type in path_data." && false);
			break;
		}
	}
	return{ pt0.x(), pt0.y(), pt1.x() - pt0.x(), pt1.y() - pt0.y() };
}

void path_factory::clear() {
	_Data.clear();
	_Has_current_point = { };
	_Current_point = { };
	_Extents_pt0 = { };
	_Extents_pt1 = { };
	_Transform_matrix = matrix_2d::init_identity();
	_Origin = { };
}
