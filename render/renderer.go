package render

import (
	"fmt"
	"log"
	"sync"

	"github.com/vulkan-go/vulkan"

	"physics-engine-go/core"
	"physics-engine-go/physics"
)

type SceneSnapshot struct {
	Time   core.TimeSnapshot
	PlaneY float64
	Bodies []physics.Body
	Lights []Light
}

type VulkanRenderer struct {
	mu       sync.RWMutex
	lights   []Light
	devices  []PhysicalDeviceInfo
	ready    bool
	headless bool
	planeY   float64
	native   *nativeRenderer
	instance vulkan.Instance
}

type PhysicalDeviceInfo struct {
	Name       string
	Type       vulkan.PhysicalDeviceType
	APIVersion uint32
	VendorID   uint32
	DeviceID   uint32
}

func NewVulkanRenderer(appName string) (*VulkanRenderer, error) {
	if native, err := newNativeRenderer(appName, 1280, 720); err == nil {
		renderer := &VulkanRenderer{
			ready:  true,
			native: native,
			planeY: 0,
			devices: []PhysicalDeviceInfo{
				{
					Name: native.DeviceName(),
				},
			},
		}
		log.Printf("windowed Vulkan renderer ready: device=%s", native.DeviceName())
		return renderer, nil
	} else {
		log.Printf("windowed Vulkan renderer failed: %v", err)
	}

	log.Printf("windowed Vulkan renderer unavailable, falling back to headless mode")
	return newHeadlessRenderer(appName)
}

func newHeadlessRenderer(appName string) (*VulkanRenderer, error) {
	if err := vulkan.SetDefaultGetInstanceProcAddr(); err != nil {
		return nil, fmt.Errorf("load vulkan loader: %w", err)
	}
	if err := vulkan.Init(); err != nil {
		return nil, fmt.Errorf("init vulkan: %w", err)
	}

	appInfo := &vulkan.ApplicationInfo{
		SType:              vulkan.StructureTypeApplicationInfo,
		PApplicationName:   appName,
		ApplicationVersion: vulkan.MakeVersion(0, 1, 0),
		PEngineName:        "PhysicsEngineGo",
		EngineVersion:      vulkan.MakeVersion(0, 1, 0),
		ApiVersion:         vulkan.MakeVersion(1, 0, 0),
	}
	defer appInfo.Free()

	instanceInfo := &vulkan.InstanceCreateInfo{
		SType:            vulkan.StructureTypeInstanceCreateInfo,
		PApplicationInfo: appInfo,
	}
	defer instanceInfo.Free()

	var instance vulkan.Instance
	if res := vulkan.CreateInstance(instanceInfo, nil, &instance); res != vulkan.Success {
		return nil, fmt.Errorf("create instance: vk result %d", res)
	}
	if err := vulkan.InitInstance(instance); err != nil {
		vulkan.DestroyInstance(instance, nil)
		return nil, fmt.Errorf("init vulkan instance functions: %w", err)
	}

	renderer := &VulkanRenderer{
		ready:    true,
		headless: true,
		planeY:   0,
		instance: instance,
	}
	if err := renderer.capturePhysicalDevices(); err != nil {
		renderer.Close()
		return nil, err
	}

	log.Printf("headless Vulkan renderer ready: %d physical device(s) detected", len(renderer.devices))
	return renderer, nil
}

func (r *VulkanRenderer) capturePhysicalDevices() error {
	var count uint32
	if res := vulkan.EnumeratePhysicalDevices(r.instance, &count, nil); res != vulkan.Success {
		return fmt.Errorf("enumerate physical devices count: vk result %d", res)
	}
	if count == 0 {
		return fmt.Errorf("no Vulkan physical devices found")
	}

	devices := make([]vulkan.PhysicalDevice, count)
	if res := vulkan.EnumeratePhysicalDevices(r.instance, &count, devices); res != vulkan.Success {
		return fmt.Errorf("enumerate physical devices: vk result %d", res)
	}

	r.devices = make([]PhysicalDeviceInfo, 0, count)
	for _, device := range devices {
		var props vulkan.PhysicalDeviceProperties
		vulkan.GetPhysicalDeviceProperties(device, &props)
		props.Deref()
		r.devices = append(r.devices, PhysicalDeviceInfo{
			Name:       vulkan.ToString(props.DeviceName[:]),
			Type:       props.DeviceType,
			APIVersion: props.ApiVersion,
			VendorID:   props.VendorID,
			DeviceID:   props.DeviceID,
		})
	}
	return nil
}

func (r *VulkanRenderer) AddLight(light Light) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.lights = append(r.lights, light)
}

func (r *VulkanRenderer) Lights() []Light {
	r.mu.RLock()
	defer r.mu.RUnlock()

	out := make([]Light, len(r.lights))
	copy(out, r.lights)
	return out
}

func (r *VulkanRenderer) Devices() []PhysicalDeviceInfo {
	out := make([]PhysicalDeviceInfo, len(r.devices))
	copy(out, r.devices)
	return out
}

func (r *VulkanRenderer) SetPlaneY(y float64) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.planeY = y
}

func (r *VulkanRenderer) PlaneY() float64 {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.planeY
}

func (r *VulkanRenderer) IsInteractive() bool {
	return r.native != nil
}

func (r *VulkanRenderer) ShouldClose() bool {
	if r.native == nil {
		return false
	}
	return r.native.ShouldClose()
}

func (r *VulkanRenderer) Render(scene SceneSnapshot) error {
	if !r.ready {
		return fmt.Errorf("renderer is not ready")
	}

	if r.native != nil {
		return r.native.Render(scene)
	}

	if scene.Time.Frame == 1 || scene.Time.Frame%60 == 0 {
		log.Printf(
			"frame=%d elapsed=%.2fs bodies=%d lights=%d",
			scene.Time.Frame,
			scene.Time.ElapsedSeconds,
			len(scene.Bodies),
			len(scene.Lights),
		)
	}
	return nil
}

func (r *VulkanRenderer) Close() {
	if r.native != nil {
		r.native.Close()
		r.native = nil
	}
	if r.instance != nil {
		vulkan.DestroyInstance(r.instance, nil)
		r.instance = nil
	}
	r.ready = false
}
