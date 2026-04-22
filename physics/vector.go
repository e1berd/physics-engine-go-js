package physics

import "fmt"

type Vec3 struct {
	X float64
	Y float64
	Z float64
}

func (v Vec3) Add(other Vec3) Vec3 {
	return Vec3{X: v.X + other.X, Y: v.Y + other.Y, Z: v.Z + other.Z}
}

func (v Vec3) Sub(other Vec3) Vec3 {
	return Vec3{X: v.X - other.X, Y: v.Y - other.Y, Z: v.Z - other.Z}
}

func (v Vec3) Mul(scalar float64) Vec3 {
	return Vec3{X: v.X * scalar, Y: v.Y * scalar, Z: v.Z * scalar}
}

func (v Vec3) Div(scalar float64) Vec3 {
	if scalar == 0 {
		return Vec3{}
	}
	return Vec3{X: v.X / scalar, Y: v.Y / scalar, Z: v.Z / scalar}
}

func (v Vec3) Dot(other Vec3) float64 {
	return v.X*other.X + v.Y*other.Y + v.Z*other.Z
}

func (v Vec3) LengthSquared() float64 {
	return v.Dot(v)
}

func (v Vec3) Length() float64 {
	return sqrt(v.LengthSquared())
}

func (v Vec3) Normalize() Vec3 {
	length := v.Length()
	if length == 0 {
		return Vec3{}
	}
	return v.Div(length)
}

func (v Vec3) String() string {
	return fmt.Sprintf("(%.2f, %.2f, %.2f)", v.X, v.Y, v.Z)
}
