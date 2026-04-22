package engine

import (
	"fmt"
	"os"
	"time"

	"physics-engine-go/core"
	"physics-engine-go/physics"
	"physics-engine-go/render"
	"physics-engine-go/script"
)

type Config struct {
	ScriptPath  string
	FixedDelta  time.Duration
	TargetFrame time.Duration
	MaxFrames   int
}

type Engine struct {
	cfg      Config
	time     *core.TimeState
	world    *physics.World
	renderer *render.VulkanRenderer
	runtime  *script.Runtime
}

func New(cfg Config) (*Engine, error) {
	if cfg.FixedDelta <= 0 {
		cfg.FixedDelta = time.Second / 60
	}
	if cfg.TargetFrame <= 0 {
		cfg.TargetFrame = cfg.FixedDelta
	}

	world := physics.NewWorld()
	renderer, err := render.NewVulkanRenderer("Physics Engine Go")
	if err != nil {
		return nil, fmt.Errorf("create renderer: %w", err)
	}
	if cfg.MaxFrames <= 0 && !renderer.IsInteractive() {
		cfg.MaxFrames = 300
	}

	rt, err := script.NewRuntime(script.Bindings{
		World:    world,
		Renderer: renderer,
	})
	if err != nil {
		renderer.Close()
		return nil, fmt.Errorf("create runtime: %w", err)
	}

	return &Engine{
		cfg:      cfg,
		time:     core.NewTimeState(cfg.FixedDelta),
		world:    world,
		renderer: renderer,
		runtime:  rt,
	}, nil
}

func (e *Engine) Run() error {
	contents, err := os.ReadFile(e.cfg.ScriptPath)
	if err != nil {
		return fmt.Errorf("read script %q: %w", e.cfg.ScriptPath, err)
	}

	if err := e.runtime.LoadScript(e.cfg.ScriptPath, string(contents)); err != nil {
		return fmt.Errorf("load script: %w", err)
	}

	if err := e.runtime.CallOnStart(e.time.Snapshot()); err != nil {
		return fmt.Errorf("call onStart: %w", err)
	}

	ticker := time.NewTicker(e.cfg.TargetFrame)
	defer ticker.Stop()

	startedAt := time.Now()
	lastTick := startedAt

	for frame := 0; ; frame++ {
		<-ticker.C

		now := time.Now()
		e.time.Advance(now.Sub(lastTick))
		lastTick = now

		snapshot := e.time.Snapshot()
		if err := e.runtime.CallOnUpdate(snapshot); err != nil {
			return fmt.Errorf("call onUpdate: %w", err)
		}

		e.world.Step(snapshot.FixedDeltaSeconds)

		scene := render.SceneSnapshot{
			Time:   snapshot,
			Bodies: e.world.Bodies(),
			Lights: e.renderer.Lights(),
		}

		if err := e.renderer.Render(scene); err != nil {
			return fmt.Errorf("render frame %d: %w", frame, err)
		}
		if e.renderer.ShouldClose() {
			break
		}
		if e.cfg.MaxFrames > 0 && frame+1 >= e.cfg.MaxFrames {
			break
		}
	}

	return nil
}

func (e *Engine) Close() {
	if e.runtime != nil {
		e.runtime.Close()
	}
	if e.renderer != nil {
		e.renderer.Close()
	}
}
