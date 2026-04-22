package script

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"log"

	"rogchap.com/v8go"

	"physics-engine-go/core"
	"physics-engine-go/physics"
	"physics-engine-go/render"
)

//go:embed engine.js
var engineBootstrap string


type Bindings struct {
	World    *physics.World
	Renderer *render.VulkanRenderer
}

type Runtime struct {
	iso      *v8go.Isolate
	ctx      *v8go.Context
	world    *physics.World
	renderer *render.VulkanRenderer
}

func NewRuntime(bindings Bindings) (*Runtime, error) {
	iso := v8go.NewIsolate()
	global := v8go.NewObjectTemplate(iso)

	rt := &Runtime{
		iso:      iso,
		world:    bindings.World,
		renderer: bindings.Renderer,
	}

	if err := rt.registerGlobals(global); err != nil {
		iso.Dispose()
		return nil, err
	}

	ctx := v8go.NewContext(iso, global)
	rt.ctx = ctx

	if err := rt.bootstrapRuntime(); err != nil {
		rt.Close()
		return nil, err
	}

	return rt, nil
}

func (r *Runtime) registerGlobals(global *v8go.ObjectTemplate) error {
	mustSet := func(name string, value interface{}) error {
		if err := global.Set(name, value); err != nil {
			return fmt.Errorf("set global %s: %w", name, err)
		}
		return nil
	}

	if err := mustSet("log", v8go.NewFunctionTemplate(r.iso, r.logCallback)); err != nil {
		return err
	}
	if err := mustSet("spawnBody", v8go.NewFunctionTemplate(r.iso, r.spawnBodyCallback)); err != nil {
		return err
	}
	if err := mustSet("applyForce", v8go.NewFunctionTemplate(r.iso, r.applyForceCallback)); err != nil {
		return err
	}
	if err := mustSet("setGravity", v8go.NewFunctionTemplate(r.iso, r.setGravityCallback)); err != nil {
		return err
	}
	if err := mustSet("addLight", v8go.NewFunctionTemplate(r.iso, r.addLightCallback)); err != nil {
		return err
	}
	if err := mustSet("spawnPlane", v8go.NewFunctionTemplate(r.iso, r.spawnPlaneCallback)); err != nil {
		return err
	}
	if err := mustSet("getBodiesJSON", v8go.NewFunctionTemplate(r.iso, r.getBodiesCallback)); err != nil {
		return err
	}
	if err := mustSet("getRendererInfoJSON", v8go.NewFunctionTemplate(r.iso, r.getRendererInfoCallback)); err != nil {
		return err
	}
	return nil
}

func (r *Runtime) bootstrapRuntime() error {
	_, err := r.ctx.RunScript(engineBootstrap, "engine.js")
	return err
}

func (r *Runtime) LoadScript(name, source string) error {
	_, err := r.ctx.RunScript(source, name)
	return err
}

func (r *Runtime) CallOnStart(snapshot core.TimeSnapshot) error {
	return r.callLifecycle("engine._start", snapshot)
}

func (r *Runtime) CallOnUpdate(snapshot core.TimeSnapshot) error {
	return r.callLifecycle("engine._update", snapshot)
}

func (r *Runtime) callLifecycle(target string, snapshot core.TimeSnapshot) error {
	if err := r.setTime(snapshot); err != nil {
		return err
	}

	source := fmt.Sprintf(`
if (%s) {
  %s(engine.time);
}
`, target, target)

	_, err := r.ctx.RunScript(source, "engine_lifecycle.js")
	return err
}

func (r *Runtime) setTime(snapshot core.TimeSnapshot) error {
	payload, err := json.Marshal(snapshot)
	if err != nil {
		return err
	}

	source := fmt.Sprintf(`
(() => {
  const next = %s;
  engine.time.frame = next.Frame;
  engine.time.deltaSeconds = next.DeltaSeconds;
  engine.time.fixedDeltaSeconds = next.FixedDeltaSeconds;
  engine.time.elapsedSeconds = next.ElapsedSeconds;
})();
`, string(payload))

	_, err = r.ctx.RunScript(source, "engine_time.js")
	return err
}

func (r *Runtime) Close() {
	if r.ctx != nil {
		r.ctx.Close()
		r.ctx = nil
	}
	if r.iso != nil {
		r.iso.Dispose()
		r.iso = nil
	}
}

func (r *Runtime) logCallback(info *v8go.FunctionCallbackInfo) *v8go.Value {
	args := info.Args()
	parts := make([]any, 0, len(args))
	for _, arg := range args {
		parts = append(parts, arg.String())
	}
	log.Println(parts...)
	return nil
}

func (r *Runtime) spawnBodyCallback(info *v8go.FunctionCallbackInfo) *v8go.Value {
	args := info.Args()
	if len(args) == 0 {
		return newIntValue(info.Context(), 0)
	}

	var payload struct {
		Name        string     `json:"name"`
		Shape       string     `json:"shape"`
		Position    vectorJSON `json:"position"`
		Velocity    vectorJSON `json:"velocity"`
		Mass        float64    `json:"mass"`
		Radius      float64    `json:"radius"`
		Restitution float64    `json:"restitution"`
		IsStatic    bool       `json:"isStatic"`
	}
	if err := json.Unmarshal([]byte(args[0].String()), &payload); err != nil {
		log.Printf("spawnBody invalid payload: %v", err)
		return newIntValue(info.Context(), 0)
	}

	id := r.world.AddBody(physics.Body{
		Name:        payload.Name,
		Shape:       payload.Shape,
		Position:    payload.Position.vec3(),
		Velocity:    payload.Velocity.vec3(),
		Mass:        payload.Mass,
		Radius:      payload.Radius,
		Restitution: payload.Restitution,
		IsStatic:    payload.IsStatic,
	})
	return newIntValue(info.Context(), int32(id))
}

func (r *Runtime) applyForceCallback(info *v8go.FunctionCallbackInfo) *v8go.Value {
	args := info.Args()
	if len(args) < 2 {
		return newBoolValue(info.Context(), false)
	}

	var payload vectorJSON
	if err := json.Unmarshal([]byte(args[1].String()), &payload); err != nil {
		log.Printf("applyForce invalid payload: %v", err)
		return newBoolValue(info.Context(), false)
	}

	ok := r.world.ApplyForce(int(args[0].Int32()), payload.vec3())
	return newBoolValue(info.Context(), ok)
}

func (r *Runtime) setGravityCallback(info *v8go.FunctionCallbackInfo) *v8go.Value {
	args := info.Args()
	if len(args) == 0 {
		return nil
	}

	var payload vectorJSON
	if err := json.Unmarshal([]byte(args[0].String()), &payload); err != nil {
		log.Printf("setGravity invalid payload: %v", err)
		return nil
	}
	r.world.SetGravity(payload.vec3())
	return nil
}

func (r *Runtime) spawnPlaneCallback(info *v8go.FunctionCallbackInfo) *v8go.Value {
	args := info.Args()
	if len(args) == 0 {
		return newBoolValue(info.Context(), false)
	}

	var payload struct {
		Y float64 `json:"y"`
	}
	if err := json.Unmarshal([]byte(args[0].String()), &payload); err != nil {
		log.Printf("spawnPlane invalid payload: %v", err)
		return newBoolValue(info.Context(), false)
	}

	r.world.SetGroundY(payload.Y)
	r.renderer.SetPlaneY(payload.Y)
	return newBoolValue(info.Context(), true)
}

func (r *Runtime) addLightCallback(info *v8go.FunctionCallbackInfo) *v8go.Value {
	args := info.Args()
	if len(args) == 0 {
		return nil
	}

	var payload struct {
		Name      string     `json:"name"`
		Kind      string     `json:"kind"`
		Position  vectorJSON `json:"position"`
		Color     vectorJSON `json:"color"`
		Intensity float64    `json:"intensity"`
	}
	if err := json.Unmarshal([]byte(args[0].String()), &payload); err != nil {
		log.Printf("addLight invalid payload: %v", err)
		return nil
	}

	if payload.Kind == "" {
		payload.Kind = "point"
	}
	if payload.Intensity == 0 {
		payload.Intensity = 1
	}

	r.renderer.AddLight(render.Light{
		Name:      payload.Name,
		Kind:      payload.Kind,
		Position:  payload.Position.vec3(),
		Color:     payload.Color.vec3(),
		Intensity: payload.Intensity,
	})
	return nil
}

func (r *Runtime) getBodiesCallback(info *v8go.FunctionCallbackInfo) *v8go.Value {
	data, err := json.Marshal(r.world.Bodies())
	if err != nil {
		log.Printf("getBodies marshal failed: %v", err)
		return newStringValue(info.Context(), "[]")
	}
	return newStringValue(info.Context(), string(data))
}

func (r *Runtime) getRendererInfoCallback(info *v8go.FunctionCallbackInfo) *v8go.Value {
	payload := struct {
		Devices []render.PhysicalDeviceInfo `json:"devices"`
		Lights  []render.Light              `json:"lights"`
	}{
		Devices: r.renderer.Devices(),
		Lights:  r.renderer.Lights(),
	}

	data, err := json.Marshal(payload)
	if err != nil {
		log.Printf("getRendererInfo marshal failed: %v", err)
		return newStringValue(info.Context(), "{}")
	}
	return newStringValue(info.Context(), string(data))
}

type vectorJSON struct {
	X float64 `json:"x"`
	Y float64 `json:"y"`
	Z float64 `json:"z"`
}

func (v vectorJSON) vec3() physics.Vec3 {
	return physics.Vec3{X: v.X, Y: v.Y, Z: v.Z}
}

func newStringValue(ctx *v8go.Context, value string) *v8go.Value {
	v, err := v8go.NewValue(ctx.Isolate(), value)
	if err != nil {
		return nil
	}
	return v
}

func newBoolValue(ctx *v8go.Context, value bool) *v8go.Value {
	v, err := v8go.NewValue(ctx.Isolate(), value)
	if err != nil {
		return nil
	}
	return v
}

func newIntValue(ctx *v8go.Context, value int32) *v8go.Value {
	v, err := v8go.NewValue(ctx.Isolate(), value)
	if err != nil {
		return nil
	}
	return v
}
