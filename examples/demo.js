log("booting physics engine")

let boxBase, sphereTop, boxMid, sphereDrop

engine.onStart((time) => {
  const { devices } = engine.getRendererInfo()
  log("renderer devices:", JSON.stringify(devices))

  const scene = new Scene()

  const camera = new PerspectiveCamera(75)
  camera.position.set(0, 5.6, 12)
  camera.lookAt(0, 1.5, 0)


  const sun = new DirectionalLight(0xf2e8db, 4.0)
  sun.name = 'sun'
  sun.position.set(8, 12, 6)
  scene.add(sun)
  const ground = new Mesh(new PlaneGeometry(), new MeshStandardMaterial())
  ground.position.y = 0
  scene.add(ground)
  boxBase = new Mesh(
    new BoxGeometry(0.72),
    new MeshStandardMaterial({ mass: 2.2, restitution: 0.42 })
  )
  boxBase.name = 'box-base'
  boxBase.position.set(-1.4, 1.0, 0.0)
  scene.add(boxBase)
  sphereTop = new Mesh(
    new SphereGeometry(0.52),
    new MeshStandardMaterial({ mass: 1.1, restitution: 0.78 })
  )
  sphereTop.name = 'sphere-top'
  sphereTop.position.set(-1.1, 3.3, 0.0)
  scene.add(sphereTop)
  boxMid = new Mesh(
    new BoxGeometry(0.62),
    new MeshStandardMaterial({ mass: 1.5, restitution: 0.50 })
  )
  boxMid.name = 'box-mid'
  boxMid.position.set(1.3, 2.0, 0.0)
  scene.add(boxMid)
  sphereDrop = new Mesh(
    new SphereGeometry(0.5),
    new MeshStandardMaterial({ mass: 1.0, restitution: 0.82 })
  )
  sphereDrop.name = 'sphere-drop'
  sphereDrop.position.set(1.3, 5.2, 0.0)
  scene.add(sphereDrop)
  engine.setScene(scene)
  log("scene ready")
})

engine.onUpdate((time) => {
  if (time.frame < 150) {
    engine.applyForce(sphereTop, new Vector3(0.25, 0, 0))
    engine.applyForce(sphereDrop, new Vector3(-0.2, 0, 0))
  }

  if (time.frame % 120 === 0) {
    log("frame", time.frame, "bodies", JSON.stringify(engine.getBodies()))
  }
})
