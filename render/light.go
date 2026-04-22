package render

import "physics-engine-go/physics"

type Light struct {
	Name      string
	Kind      string
	Position  physics.Vec3
	Color     physics.Vec3
	Intensity float64
}
