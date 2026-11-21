# Field

A two-column property row (label, control) used by Inspector, Details and Project Settings.

- The label column is at least 80px and 40% wide, `label` style in `fg-dim`. Controls are 22px (`size-row`) on `sunk` with a `line` border; focus turns the border `accent`.
- Vectors use one input per axis with a 14px color tab: X `axis-x`, Y `axis-y`, Z `axis-z`.
- Paths, ids and keys inside inputs use the `mono` family.
- A value that differs from its prefab (`is-override`) gets a 2px `prefab` bar on the left and a bold label.
- Component groups start with a `bdg-comp-head` bar on `raise`.
- Provide: a label per control, the current value, and for selects the full option list.
- Do not put help text inside the row; place it in a note below the group.
