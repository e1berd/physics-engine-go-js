package main

import (
	"log"
	"os"
	"runtime"
	"time"

	"physics-engine-go/engine"
)

func main() {
	runtime.LockOSThread()
	scriptPath := "examples/demo.js"
	for {
		app, err := engine.New(engine.Config{
			ScriptPath:  scriptPath,
			FixedDelta:  time.Second / 60,
			MaxFrames:   0,
			TargetFrame: time.Second / 60,
		})
		if err != nil {
			log.Fatalf("engine init: %v", err)
		}
		done := make(chan struct{})
		go watchAndStop(scriptPath, app, done)
		runErr := app.Run()
		close(done)
		app.Close()
		if runErr != nil {
			log.Fatalf("engine run: %v", runErr)
		}
		if !app.WasRestartRequested() {
			break
		}
		log.Printf("script %q changed, reloading...", scriptPath)
	}
}
func watchAndStop(path string, app *engine.Engine, done <-chan struct{}) {
	info, _ := os.Stat(path)
	var lastMod time.Time
	if info != nil {
		lastMod = info.ModTime()
	}
	ticker := time.NewTicker(250 * time.Millisecond)
	defer ticker.Stop()
	for {
		select {
		case <-done:
			return
		case <-ticker.C:
			fi, err := os.Stat(path)
			if err == nil && fi.ModTime().After(lastMod) {
				app.RequestRestart()
				return
			}
		}
	}
}
