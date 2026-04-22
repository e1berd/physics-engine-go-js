log("booting js runtime")

engine.onStart((time) => {
  const { devices } = engine.getRendererInfo()
  log("renderer devices:", JSON.stringify(devices))

  const scene = new Scene()
    .gravity(new Point(0, -9.81, 0))

  const camera = new Camera()
    .position(new Point(0, 5.6, 12))
    .target(new Point(0, 1.5, 0))

  const light = new DirectionalLight('sun')
    .position(new Point(8, 12, 6))
    .color(new Color(1.0, 0.95, 0.86))
    .intensity(4.0)

  scene.add(light)
  scene.add(camera)

  globalThis.figures = [
    scene.add(new RigidBody('box-base')
      .shape('box')
      .position(new Point(-1.1, 1.0, 0.0))
      .mass(2.2).radius(0.72).restitution(0.42)),

    scene.add(new RigidBody('sphere-top')
      .shape('sphere')
      .position(new Point(-1.1, 3.3, 0.0))
      .mass(1.1).radius(0.52).restitution(0.78)),

    scene.add(new RigidBody('box-mid')
      .shape('box')
      .position(new Point(1.3, 2.0, 0.0))
      .mass(1.5).radius(0.62).restitution(0.50)),

    scene.add(new RigidBody('sphere-drop')
      .shape('sphere')
      .position(new Point(1.3, 5.2, 0.0))
      .mass(1.0).radius(0.5).restitution(0.82)),
  ]


  engine.setScene(scene)
  log("scene ready")
})

engine.onUpdate((time) => {
  if (time.frame < 150) {
    engine.applyForce(figures[1], new Point(0.25, 0, 0))
    engine.applyForce(figures[3], new Point(-0.2, 0, 0))
  }

  if (time.frame % 120 === 0) {
    log("frame", time.frame, "bodies", JSON.stringify(engine.getBodies()))
  }
})
