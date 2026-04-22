package main

import (
	"log"
	"runtime"
	"time"

	"physics-engine-go/engine"
)

func main() {
	runtime.LockOSThread()

	cfg := engine.Config{
		ScriptPath:  "examples/demo.js",
		FixedDelta:  time.Second / 60,
		MaxFrames:   0,
		TargetFrame: time.Second / 60,
	}

	app, err := engine.New(cfg)
	if err != nil {
		log.Fatalf("engine init failed: %v", err)
	}
	defer app.Close()

	if err := app.Run(); err != nil {
		log.Fatalf("engine run failed: %v", err)
	}
}
