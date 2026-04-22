package physics

type Body struct {
	ID          int
	Name        string
	Shape       string
	Position    Vec3
	Velocity    Vec3
	Force       Vec3
	Mass        float64
	Radius      float64
	Restitution float64
	Roughness   float64
	Metalness   float64
	Color       Vec3
	IsStatic    bool
}

func (b *Body) inverseMass() float64 {
	if b.IsStatic || b.Mass <= 0 {
		return 0
	}
	return 1.0 / b.Mass
}
