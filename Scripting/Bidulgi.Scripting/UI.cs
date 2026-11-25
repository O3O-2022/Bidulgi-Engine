// In-game UI from C# (queue 7-3), the easy-API way: elements are objects with a UIElement under a UICanvas, named
// in the scene; scripts reach them by name.
//
//   UI.Text("score").text = $"점수 {score}";
//   UI.Bar("health").value = hp / 100f;
//   if (UI.Button("restart").clicked) Restart();          // true once per click
//   UI.Button("restart").OnClick(Restart);                  // or: called on each click while this behaviour runs
//   UI.Image("crosshair").visible = aiming;
//
// A missing element is a warning and a stand-in that ignores everything (no exception), like Find.
using System;
using System.Collections.Generic;
using System.Numerics;
using System.Text.Json.Nodes;

namespace Bidulgi;

public class UIWidget {
  internal readonly GameObject go;
  readonly UIElement? element;
  internal UIWidget(GameObject go) {
    this.go = go;
    element = go.Id.Length > 0 ? go.GetComponent<UIElement>() : null;
  }
  public bool exists => element != null;
  public GameObject gameObject => go;
  public bool visible { get => element?.Visible ?? false; set { if (element != null && element.Visible != value) element.Visible = value; } }
  public string text { get => element?.Text ?? ""; set { if (element != null && element.Text != value) element.Text = value; } }
  public Vector4 color { get => element?.Color ?? Vector4.Zero; set { if (element != null) element.Color = value; } }
  public Vector4 textColor { get => element?.TextColor ?? Vector4.Zero; set { if (element != null) element.TextColor = value; } }
  public float fontSize { get => element?.FontSize ?? 0; set { if (element != null) element.FontSize = value; } }
  public Vector2 position { get => element?.Position ?? Vector2.Zero; set { if (element != null) element.Position = value; } }
  public Vector2 size { get => element?.Size ?? Vector2.Zero; set { if (element != null) element.Size = value; } }
  internal UIElement? Element => element;
}

public sealed class UIText : UIWidget { internal UIText(GameObject go) : base(go) {} }
public sealed class UIPanel : UIWidget { internal UIPanel(GameObject go) : base(go) {} }
public sealed class UIImage : UIWidget {
  internal UIImage(GameObject go) : base(go) {}
  public string image { get => Element?.Image ?? ""; set { if (Element != null) Element.Image = value; } }
}
public sealed class UIBar : UIWidget {
  internal UIBar(GameObject go) : base(go) {}
  // 0..1
  public float value { get => Element?.Value ?? 0; set { float v = Math.Clamp(value, 0, 1); if (Element != null && MathF.Abs(Element.Value - v) > 1e-4f) Element.Value = v; } }
}
public sealed class UIButton : UIWidget {
  internal UIButton(GameObject go) : base(go) {}
  // Clicks since the game started.
  public int clicks => Element?.Clicks ?? 0;
  // True once for each click (reading it takes the click).
  public bool clicked {
    get {
      int now = clicks, seen = UI.SeenClicks(go.Id);
      if (now <= seen) return false;
      UI.SetSeenClicks(go.Id, seen + 1);
      return true;
    }
  }
  public bool interactable { get => Element?.Interactable ?? false; set { if (Element != null) Element.Interactable = value; } }
  // Calls `action` on every click while the calling behaviour runs (cancelled with it, like After / Every).
  public Timer OnClick(Action action) => Game.Every(1e-4f, () => { while (clicked) action(); }, first: 0);
}

public static class UI {
  static readonly Dictionary<string, GameObject> s_byName = new();
  static readonly Dictionary<string, int> s_seen = new();

  public static UIText Text(string name) => new(Get(name));
  public static UIButton Button(string name) => new(Get(name));
  public static UIImage Image(string name) => new(Get(name));
  public static UIPanel Panel(string name) => new(Get(name));
  public static UIBar Bar(string name) => new(Get(name));

  static GameObject Get(string name) {
    if (s_byName.TryGetValue(name, out GameObject? g) && g.IsValid) return g;
    GameObject? found = GameObject.Find(name);
    if (found == null || !found.HasComponent("UIElement")) {
      Debug.LogWarning($"UI: no element \"{name}\" (an object with a UIElement under a UICanvas)");
      return GameObject.None;
    }
    s_byName[name] = found;
    return found;
  }

  internal static int SeenClicks(string id) => s_seen.TryGetValue(id, out int n) ? n : 0;
  internal static void SetSeenClicks(string id, int n) => s_seen[id] = n;
  internal static void Reset() { s_byName.Clear(); s_seen.Clear(); } // play stopped
}
