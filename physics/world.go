package physics

import (
	"math"
	"slices"
	"sync"
)

type World struct {
	mu      sync.RWMutex
	nextID  int
	gravity Vec3
	groundY float64
	bodies  map[int]*Body
}

func NewWorld() *World {
	return &World{
		nextID:  1,
		gravity: Vec3{X: 0, Y: -9.81, Z: 0},
		groundY: 0,
		bodies:  make(map[int]*Body),
	}
}

func (w *World) SetGravity(v Vec3) {
	w.mu.Lock()
	defer w.mu.Unlock()
	w.gravity = v
}

func (w *World) Gravity() Vec3 {
	w.mu.RLock()
	defer w.mu.RUnlock()
	return w.gravity
}

func (w *World) SetGroundY(y float64) {
	w.mu.Lock()
	defer w.mu.Unlock()
	w.groundY = y
}

func (w *World) GroundY() float64 {
	w.mu.RLock()
	defer w.mu.RUnlock()
	return w.groundY
}

func (w *World) AddBody(body Body) int {
	w.mu.Lock()
	defer w.mu.Unlock()

	if body.Mass <= 0 {
		body.Mass = 1
	}
	if body.Radius <= 0 {
		body.Radius = 0.5
	}
	if body.Restitution <= 0 {
		body.Restitution = 0.6
	}
	if body.Shape == "" {
		body.Shape = "sphere"
	}

	body.ID = w.nextID
	w.nextID++

	copy := body
	w.bodies[body.ID] = &copy
	return body.ID
}

func (w *World) ApplyForce(id int, force Vec3) bool {
	w.mu.Lock()
	defer w.mu.Unlock()

	body, ok := w.bodies[id]
	if !ok {
		return false
	}
	body.Force = body.Force.Add(force)
	return true
}

func (w *World) Bodies() []Body {
	w.mu.RLock()
	defer w.mu.RUnlock()

	out := make([]Body, 0, len(w.bodies))
	ids := make([]int, 0, len(w.bodies))
	for id := range w.bodies {
		ids = append(ids, id)
	}
	slices.Sort(ids)
	for _, id := range ids {
		out = append(out, *w.bodies[id])
	}
	return out
}

func (w *World) Step(dt float64) {
	w.mu.Lock()
	defer w.mu.Unlock()

	for _, body := range w.bodies {
		if body.IsStatic {
			continue
		}

		acceleration := w.gravity.Add(body.Force.Mul(body.inverseMass()))
		body.Velocity = body.Velocity.Add(acceleration.Mul(dt))
		body.Velocity = body.Velocity.Mul(0.998)
		body.Position = body.Position.Add(body.Velocity.Mul(dt))
		body.Force = Vec3{}

		if body.Position.Y-body.Radius < w.groundY {
			body.Position.Y = w.groundY + body.Radius
			if body.Velocity.Y < 0 {
				body.Velocity.Y = -body.Velocity.Y * body.Restitution
			}
			body.Velocity.X *= 0.94
			body.Velocity.Z *= 0.94
			if math.Abs(body.Velocity.X) < 0.005 {
				body.Velocity.X = 0
			}
			if math.Abs(body.Velocity.Z) < 0.005 {
				body.Velocity.Z = 0
			}
		}
	}

	ids := make([]int, 0, len(w.bodies))
	for id := range w.bodies {
		ids = append(ids, id)
	}
	for i := 0; i < len(ids); i++ {
		for j := i + 1; j < len(ids); j++ {
			a := w.bodies[ids[i]]
			b := w.bodies[ids[j]]
			resolveSphereCollision(a, b)
		}
	}
}

func resolveSphereCollision(a, b *Body) {
	if a.IsStatic && b.IsStatic {
		return
	}

	delta := b.Position.Sub(a.Position)
	distanceSquared := delta.LengthSquared()
	radius := a.Radius + b.Radius
	if distanceSquared == 0 || distanceSquared >= radius*radius {
		return
	}

	distance := sqrt(distanceSquared)
	normal := delta.Div(distance)
	penetration := radius - distance

	totalInvMass := a.inverseMass() + b.inverseMass()
	if totalInvMass == 0 {
		return
	}

	correction := normal.Mul(penetration / totalInvMass)
	if !a.IsStatic {
		a.Position = a.Position.Sub(correction.Mul(a.inverseMass()))
	}
	if !b.IsStatic {
		b.Position = b.Position.Add(correction.Mul(b.inverseMass()))
	}

	relativeVelocity := b.Velocity.Sub(a.Velocity)
	separatingVelocity := relativeVelocity.Dot(normal)
	if separatingVelocity > 0 {
		return
	}

	restitution := (a.Restitution + b.Restitution) * 0.5
	impulseMagnitude := -(1 + restitution) * separatingVelocity / totalInvMass
	impulse := normal.Mul(impulseMagnitude)

	if !a.IsStatic {
		a.Velocity = a.Velocity.Sub(impulse.Mul(a.inverseMass()))
	}
	if !b.IsStatic {
		b.Velocity = b.Velocity.Add(impulse.Mul(b.inverseMass()))
	}
}
