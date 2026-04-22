package core

import "time"

type TimeSnapshot struct {
	Frame             int
	DeltaSeconds      float64
	FixedDeltaSeconds float64
	ElapsedSeconds    float64
}

type TimeState struct {
	frame      int
	delta      time.Duration
	fixedDelta time.Duration
	elapsed    time.Duration
}

func NewTimeState(fixedDelta time.Duration) *TimeState {
	return &TimeState{
		fixedDelta: fixedDelta,
		delta:      fixedDelta,
	}
}

func (t *TimeState) Advance(delta time.Duration) {
	t.frame++
	t.delta = delta
	t.elapsed += delta
}

func (t *TimeState) Snapshot() TimeSnapshot {
	return TimeSnapshot{
		Frame:             t.frame,
		DeltaSeconds:      t.delta.Seconds(),
		FixedDeltaSeconds: t.fixedDelta.Seconds(),
		ElapsedSeconds:    t.elapsed.Seconds(),
	}
}
