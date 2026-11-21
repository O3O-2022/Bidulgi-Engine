# Panel

A dockable editor panel: a 30px header of tabs on `win` over a `panel` body.

- Provide: header tabs (see Tabs), optional right-aligned controls in the header, and the body content.
- Place panels in a grid whose background is `line` with a 1px gap, so every seam is exactly one pixel. Do not draw borders between panels.
- Body rows use `space-5` side padding. Section captions use the `caption` style in `fg-faint`.
- Every editing panel offers a Text tab that shows the saved text form of what is being edited.
- Do not give a panel its own background color, rounded corners inside the window, or a drop shadow.
