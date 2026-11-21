# Button

Buttons and icon buttons for toolbars and panels.

- Default: `raise` fill, `line-strong` border, 12px `label` text, `radius-md`, 24px tall.
- Primary (`is-primary`): `accent` fill with `on-accent` text. At most one per toolbar or dialog: the action the screen exists for (Compile, Save).
- Status inside a button: an 8px dot in `ok`, `warn` or `err` plus a word ("컴파일됨").
- Icon buttons sit in a `bdg-group` on `sunk`; a pressed toggle (`aria-pressed="true"`) takes the `accent` fill.
- Provide: a text label, or `aria-label` for icon-only buttons; 14px inline SVG icons using `currentColor`.
- Do not use gradients, shadows, or the accent on more than one button in a group.
