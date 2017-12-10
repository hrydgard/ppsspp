#pragma once

#include <cmath>

struct Point {
	Point() {}
	Point(float x_, float y_) : x(x_), y(y_) {}

	float x;
	float y;

	float distanceTo(const Point &other) const {
		float dx = other.x - x, dy = other.y - y;
		return sqrtf(dx*dx + dy*dy);
	}

	bool operator ==(const Point &other) const {
		return x == other.x && y == other.y;
	}

	/*
	FocusDirection directionTo(const Point &other) const {
		int angle = atan2f(other.y - y, other.x - x) / (2 * M_PI) - 0.125;

	}*/
};


// Resolved bounds on screen after layout.
struct Bounds {
	Bounds() : x(0), y(0), w(0), h(0) {}
	Bounds(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

	bool Contains(float px, float py) const {
		return (px >= x && py >= y && px < x + w && py < y + h);
	}

	bool Intersects(const Bounds &other) const {
		return !(x > other.x2() || x2() < other.x || y > other.y2() || y2() < other.y);
	}

	void Clip(const Bounds &clipTo) {
		if (x < clipTo.x) {
			w -= clipTo.x - x;
			x = clipTo.x;
		}
		if (y < clipTo.y) {
			h -= clipTo.y - y;
			y = clipTo.y;
		}
		if (x2() > clipTo.x2()) {
			w = clipTo.x2() - x;
		}
		if (y2() > clipTo.y2()) {
			h = clipTo.y2() - y;
		}
	}

	float x2() const { return x + w; }
	float y2() const { return y + h; }
	float centerX() const { return x + w * 0.5f; }
	float centerY() const { return y + h * 0.5f; }
	Point Center() const {
		return Point(centerX(), centerY());
	}
	Bounds Expand(float amount) const {
		return Bounds(x - amount, y - amount, w + amount * 2, h + amount * 2);
	}
	Bounds Offset(float xAmount, float yAmount) const {
		return Bounds(x + xAmount, y + yAmount, w, h);
	}

	float x;
	float y;
	float w;
	float h;
};


