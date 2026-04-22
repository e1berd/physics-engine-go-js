// ── Value types ──────────────────────────────────────────────────────────────
class Vector3 {
  constructor(x = 0, y = 0, z = 0) { this.x = x; this.y = y; this.z = z; }
  set(x, y, z) { this.x = x; this.y = y; this.z = z; return this; }
  copy(v) { this.x = v.x; this.y = v.y; this.z = v.z; return this; }
  clone() { return new Vector3(this.x, this.y, this.z); }
}
class Euler {
  constructor(x = 0, y = 0, z = 0) { this.x = x; this.y = y; this.z = z; }
  set(x, y, z) { this.x = x; this.y = y; this.z = z; return this; }
}
class Color {
  constructor(r = 1, g = 1, b = 1) {
    // Handle hex color: Color(0xff0000)
    if (typeof r === 'number' && arguments.length === 1) {
      this.r = ((r >> 16) & 0xff) / 255;
      this.g = ((r >> 8) & 0xff) / 255;
      this.b = (r & 0xff) / 255;
    } else {
      this.r = r; this.g = g; this.b = b;
    }
  }
  set(r, g, b) { this.r = r; this.g = g; this.b = b; return this; }
}
// ── Base object ───────────────────────────────────────────────────────────────
class Object3D {
  constructor() {
    this.name = '';
    this.position = new Vector3();
    this.rotation = new Euler();
    this._id = null;
  }
}
// ── Camera ────────────────────────────────────────────────────────────────────
class PerspectiveCamera extends Object3D {
  constructor(fov = 75, near = 0.1, far = 1000) {
    super();
    this.fov = fov;
    this.near = near;
    this.far = far;
    this.position.set(0, 5.6, 12);
    this._target = new Vector3(0, 1.5, 0);
  }
  lookAt(x, y, z) {
    if (x instanceof Vector3) { this._target.copy(x); }
    else { this._target.set(x, y, z); }
    return this;
  }
}
// ── Geometry ──────────────────────────────────────────────────────────────────
class BoxGeometry {
  constructor(radius = 0.5) { this.type = 'box'; this.radius = radius; }
}
class SphereGeometry {
  constructor(radius = 0.5) { this.type = 'sphere'; this.radius = radius; }
}
class PlaneGeometry {
  constructor() { this.type = 'plane'; }
}
// ── Material ──────────────────────────────────────────────────────────────────
class MeshStandardMaterial {
  constructor(opts = {}) {
    const c = opts.color;
    if (c instanceof Color) { this.color = c; }
    else if (typeof c === 'number') { this.color = new Color(c); }
    else { this.color = new Color(1, 1, 1); }
    this.roughness   = opts.roughness   ?? 0.5;
    this.metalness   = opts.metalness   ?? 0.0;
    this.mass        = opts.mass        ?? 1.0;
    this.restitution = opts.restitution ?? 0.6;
  }
}
// ── Mesh ──────────────────────────────────────────────────────────────────────
class Mesh extends Object3D {
  constructor(geometry, material) {
    super();
    this.geometry  = geometry || new SphereGeometry();
    this.material  = material || new MeshStandardMaterial();
    this._velocity = new Vector3();
    this._static   = false;
  }
}
// ── Lights ────────────────────────────────────────────────────────────────────
class DirectionalLight extends Object3D {
  constructor(color = 0xffffff, intensity = 1) {
    super();
    this.color     = color instanceof Color ? color : new Color(color);
    this.intensity = intensity;
  }
}
// ── Scene ─────────────────────────────────────────────────────────────────────
class Scene {
  constructor() {
    this._objects  = [];
    this._gravity  = new Vector3(0, -9.81, 0);
    this.background = new Color(0x7ec0ee);
  }
  get gravity()  { return this._gravity; }
  set gravity(v) { this._gravity = v instanceof Vector3 ? v : new Vector3(v.x, v.y, v.z); }
  add(obj) { this._objects.push(obj); return obj; }
}
// ── Engine ────────────────────────────────────────────────────────────────────
const engine = {
  _start:  null,
  _update: null,
  time: { frame: 0, deltaSeconds: 0, fixedDeltaSeconds: 0, elapsedSeconds: 0 },
  onStart(fn)  { this._start  = fn; },
  onUpdate(fn) { this._update = fn; },
  setScene(scene) {
    const g = scene._gravity;
    setGravity(JSON.stringify({ x: g.x, y: g.y, z: g.z }));
    for (const obj of scene._objects) {
      if (obj instanceof DirectionalLight) {
        const p = obj.position, c = obj.color;
        addLight(JSON.stringify({
          name:      obj.name || 'light',
          kind:      'directional',
          position:  { x: p.x, y: p.y, z: p.z },
          color:     { x: c.r, y: c.g, z: c.b },
          intensity: obj.intensity,
        }));
      } else if (obj instanceof Mesh) {
        const geom = obj.geometry;
        if (geom.type === 'plane') {
          spawnPlane(JSON.stringify({ y: obj.position.y }));
        } else {
          const pos = obj.position, vel = obj._velocity, mat = obj.material;
          const id = spawnBody(JSON.stringify({
            name:        obj.name || '',
            shape:       geom.type,
            position:    { x: pos.x, y: pos.y, z: pos.z },
            velocity:    { x: vel.x, y: vel.y, z: vel.z },
            mass:        mat.mass,
            radius:      geom.radius,
            restitution: mat.restitution,
            roughness:   mat.roughness,
            metalness:   mat.metalness,
            color:       { x: mat.color.r, y: mat.color.g, z: mat.color.b },
            isStatic:    obj._static,
          }));
          obj._id = id;
        }
      }
    }
  },
  applyForce(obj, force) {
    const id = (typeof obj === 'number') ? obj : obj._id;
    return applyForce(id, JSON.stringify({ x: force.x, y: force.y, z: force.z }));
  },
  getBodies()       { return JSON.parse(getBodiesJSON()); },
  getRendererInfo() { return JSON.parse(getRendererInfoJSON()); },
};
globalThis.engine = engine;
