# Tabs

Two tab kinds: panel tabs inside a panel header, and workspace tabs that swap the whole panel layout.

- Panel tab: 12px `label` text in `fg-dim`; the selected tab takes the `panel` background, `fg` text and a 2px `accent` top border so it joins the body below.
- Workspace tab: 12.5px in `fg-dim` on `win`; the selected one sits on `raise` with a 2px `accent` underline.
- Provide: `role="tablist"` on the bar, `role="tab"` and `aria-selected` on each tab.
- Do not mark more than one tab per bar with the accent, or use icons without text.
