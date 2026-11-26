// Game UI controls from C# (7-3, round 7), checked by BDG_UICSHARP_TEST: UI.Toggle / Slider / Input / Dropdown /
// ScrollView read and set their values, OnChanged fires once per change (a script's or the player's), a missing
// element is a warning and a stand-in (no exception).
using Bidulgi;

namespace Bidulgi.Samples;

public class UIProbe : Behaviour {
  async void Start() {
    UIToggle sound = UI.Toggle("probe_sound");
    UISlider volume = UI.Slider("probe_volume");
    UIInput name = UI.Input("probe_name");
    UIDropdown quality = UI.Dropdown("probe_quality");
    UIScrollView list = UI.ScrollView("probe_list");
    int toggled = 0, slid = 0, typed = 0, chosen = 0;
    bool lastOn = true;
    string lastText = "", lastChoice = "";
    sound.OnChanged(on => { ++toggled; lastOn = on; });
    volume.OnChanged(_ => ++slid);
    name.OnChanged(t => { ++typed; lastText = t; });
    quality.OnChanged(i => { ++chosen; lastChoice = quality.selected; });
    bool read = sound.on && System.MathF.Abs(volume.value - 0.5f) < 1e-4f && name.text == "" && name.placeholder == "Name" && quality.index == 0 &&
                quality.options.Length == 3 && quality.selected == "Low";
    await NextFrame();
    sound.on = false;
    volume.value = 2; // clamped to 1
    name.text = "Kim";
    quality.index = 2;
    list.scroll = 40;
    await NextFrame();
    await NextFrame();
    bool set = !sound.on && volume.value == 1 && name.text == "Kim" && quality.selected == "High" && list.scroll == 40;
    bool changed = toggled == 1 && !lastOn && slid == 1 && typed == 1 && lastText == "Kim" && chosen == 1 && lastChoice == "High";
    bool missing = !UI.Toggle("probe_no_such_toggle").on && !UI.Dropdown("probe_nothing").exists;
    int B(bool v) => v ? 1 : 0;
    Debug.Log($"ui probe: read={B(read)} set={B(set)} changed={B(changed)} missing={B(missing)}");
  }
}
