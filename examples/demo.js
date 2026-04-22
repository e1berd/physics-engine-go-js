log("booting js runtime");

engine.onStart((time) => {
  const renderer = engine.getRendererInfo();
  log("renderer devices:", JSON.stringify(renderer.devices));

  engine.setGravity({ x: 0, y: -9.81, z: 0 });
  engine.spawnPlane({ y: 0.0 });

  engine.addLight({
    name: "sun",
    kind: "directional",
    position: { x: 8, y: 12, z: 6 },
    color: { x: 1.0, y: 0.95, z: 0.86 },
    intensity: 4.0,
  });

  globalThis.figures = [
    engine.spawnBody({
      name: "box-base",
      shape: "box",
      position: { x: -1.1, y: 1.0, z: 0.0 },
      velocity: { x: 0.0, y: 0.0, z: 0.0 },
      mass: 2.2,
      radius: 0.72,
      restitution: 0.42,
    }),
    engine.spawnBody({
      name: "sphere-top",
      shape: "sphere",
      position: { x: -1.1, y: 3.3, z: 0.0 },
      velocity: { x: 0.0, y: 0.0, z: 0.0 },
      mass: 1.1,
      radius: 0.52,
      restitution: 0.78,
    }),
    engine.spawnBody({
      name: "box-mid",
      shape: "box",
      position: { x: 1.3, y: 2.0, z: 0.0 },
      velocity: { x: 0.0, y: 0.0, z: 0.0 },
      mass: 1.5,
      radius: 0.62,
      restitution: 0.50,
    }),
    engine.spawnBody({
      name: "sphere-drop",
      shape: "sphere",
      position: { x: 1.3, y: 5.2, z: 0.0 },
      velocity: { x: 0.0, y: 0.0, z: 0.0 },
      mass: 1.0,
      radius: 0.5,
      restitution: 0.82,
    }),
  ];

  log("3d scene created");
});

engine.onUpdate((time) => {
  if (time.frame < 150) {
    engine.applyForce(figures[1], { x: 0.25, y: 0.0, z: 0.0 });
    engine.applyForce(figures[3], { x: -0.2, y: 0.0, z: 0.0 });
  }

  if (time.frame % 120 === 0) {
    log("frame", time.frame, "bodies", JSON.stringify(engine.getBodies()));
  }
});
