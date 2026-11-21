# Node

A graph node shared by Graph scripts, Animation Graphs and Material graphs (Dear ImGui with imgui-node-editor in the engine).

- Body `panel`, border `line-strong`, `radius-md`, `shadow-node`. Header on `win` with a 3px top stripe (`size-node-stripe`) that names the kind: event `node-event`, function `node-function`, pure `node-pure`, flow control `node-flow`.
- Header text uses `node-title`; the owning class follows in `fg-dim` 10.5px.
- Pins are 10px rings in the type color (`pin-float`, `pin-vector`, `pin-bool`, `pin-object`); execution pins are `pin-exec` triangles. Input literals sit in a `sunk` box in `mono`.
- Selected nodes get a 2px `accent` outline. Wires use the pin color; execution wires are thicker.
- Provide: kind, title, owner, input and output pins with types, literal values.
- Do not use gradient headers, kind colors as full header fills, or the accent as a node color.
