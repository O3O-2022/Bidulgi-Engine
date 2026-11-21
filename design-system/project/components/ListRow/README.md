# ListRow

One row in Hierarchy, Project, script lists and asset browsers: kind mark, name, type on the right.

- 22px tall (`size-row`), `control` text, `space-5` side padding; children indent 14px per level.
- Hover takes `raise`; the selected row takes `selection`.
- Kind marks are 11px shapes: mesh square, light circle in `warn`, camera half-rounded, prefab filled `prefab`, widget filled `pin-ui`, empty dashed.
- Prefab instances show their name in `prefab`. The type column is `fg-faint` 11px and may show a Graph class name.
- Provide: kind, name, type, depth, selected state.
- Do not use emoji as kind marks or add more than one trailing column.
