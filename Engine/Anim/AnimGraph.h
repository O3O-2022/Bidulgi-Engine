#pragma once
// Animation graph (queue 7-1, Docs/features/animation.md "업그레이드: 텍스트 저장"): the `.animgraph` text asset with
// variables, state machines (states play a clip or a blend space of clips; transitions on variables, triggers and exit
// time, each with its cross-fade) and layers (more machines over a bone mask, override or additive), plus the runtime
// that plays one machine for one Animator layer.
//
//   {"v":1, "skeleton":"Models/Hero.gltf",
//    "vars":{"Speed":"float", "MoveX":"float", "MoveY":"float", "Jump":"trigger", "Aim":"bool"},
//    "stateMachines":{
//      "Locomotion":{
//        "entry":"Move",
//        "states":{"Move":{"blendSpace":{"x":"Speed", "samples":[{"clip":"Idle","x":0}, {"clip":"Walk","x":1.6},
//                                                               {"clip":"Run","x":4.5}]},
//                          "events":[{"event":"Footstep","at":0.25}]},
//                  "Strafe":{"blendSpace":{"x":"MoveX", "y":"MoveY", "samples":[{"clip":"Idle","x":0,"y":0},
//                            {"clip":"Walk","x":0,"y":1}, {"clip":"WalkBack","x":0,"y":-1}, ...]}},
//                  "Jump":{"play":"Jump", "loop":false}},
//        "transitions":[{"from":"*", "to":"Jump", "when":"Jump", "blend":0.1},
//                       {"from":"Jump", "to":"Move", "exitTime":1, "blend":0.2}]},
//      "Arms":{"states":{"Rest":{"play":"Idle"}, "Wave":{"play":"Wave"}}, "transitions":[...]}},
//    "layers":[{"name":"UpperBody", "machine":"Arms", "fromBone":"Spine", "weight":1, "mode":"override"}]}
//
// The first state machine is the base layer (every bone). `layers` adds machines over a bone mask (`fromBone`: that
// bone and everything under it; or `bones`: a list), mixed by `weight`: "override" replaces the masked bones,
// "additive" adds the layer's motion relative to its clips' first frame.
// Blend spaces: 1D (`x`) mixes the two neighbouring samples; 2D (`x`, `y`) uses gradient bands (Unity's freeform
// cartesian): exact at every sample, smooth between. Clips of a blend space play in step (normalised time), so a
// blend state's `events` and `exitTime` are in normalised time 0..1; a clip state's `events` are in clip seconds.
// `when`: `var op number` (> >= < <= == !=), a bare bool / trigger name, `!`, `&&`, `||` (no parentheses; && binds
// tighter). A trigger is consumed by the transition that fires on it.
// Left for later: blend space files (.blend1d / .blend2d), the `pipeline` / `update` sections, graph functions
// (`{"fn":...}` in when), montages, sync groups.
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bdg {
struct AnimGraphDef {
  enum class VarType { Float, Int, Bool, Trigger };
  struct Var { std::string name; VarType type = VarType::Float; };
  struct Event { std::string name; float at = 0; };
  struct BlendSample { std::string clip; float x = 0, y = 0; };
  struct State {
    std::string name, clip; // clip: a clip name of the skeleton's model ("" = the state's name)
    bool loop = true;
    float speed = 1.f;
    std::vector<Event> events;
    std::string blendX, blendY;         // blend space variables (blendY empty: 1D)
    std::vector<BlendSample> samples;   // non-empty: a blend space state
    bool IsBlend() const { return !samples.empty(); }
  };
  struct Cond {
    enum Op { Truthy, Gt, Ge, Lt, Le, Eq, Ne };
    std::string var;
    Op op = Truthy;
    float value = 0;
    bool negate = false;
  };
  struct Transition {
    int from = -1, to = 0;                // from -1 = any state ("*")
    std::string when;                     // as written
    std::vector<std::vector<Cond>> anyOf; // OR of ANDs (empty: no condition)
    float blend = 0.2f;                   // cross-fade seconds
    float exitTime = -1.f;                // < 0: none
  };
  struct Machine {
    std::string name = "Base";
    std::vector<State> states;
    std::vector<Transition> transitions;
    int entry = 0;
    int FindState(std::string_view stateName) const;
  };
  struct Layer {
    std::string name;
    int machine = 0;
    std::string fromBone;           // mask: this bone and its descendants
    std::vector<std::string> bones; // or these bones (each with its descendants)
    float weight = 1.f;
    std::string weightVar; // "weight":"Var": the variable gives the weight (0..1) every frame
    bool additive = false;
  };
  std::string skeleton;
  std::vector<Var> vars;
  std::vector<Machine> machines; // [0] = the base layer's
  std::vector<Layer> layers;     // over the base layer, in order
  const Var* FindVar(std::string_view name) const;
  int FindMachine(std::string_view name) const;
};

bool ParseAnimGraph(std::string_view json, AnimGraphDef& out, std::string* error = nullptr);
bool ParseAnimCondition(std::string_view text, std::vector<std::vector<AnimGraphDef::Cond>>& out, std::string* error = nullptr);
std::string WriteAnimGraph(const AnimGraphDef& def); // the same layout, one state / transition per line

// Blend space weights at (x, y) for samples at (sx, sy): 1D (is2D false) or gradient band 2D. Sum 1.
void BlendSpaceWeights(const std::vector<AnimGraphDef::BlendSample>& samples, bool is2D, float x, float y, std::vector<float>& weights);

// One Animator layer's playback: a state machine of the graph (or none: clips played by name) and the shared
// variables.
class AnimatorRuntime {
public:
  using DurationFn = std::function<float(const std::string& clip)>; // clip length in seconds, < 0 = no such clip
  using Params = std::map<std::string, float>;
  struct Play {
    int state = -1;       // machine state, -1 = a clip played directly
    std::string clip;     // a clip state's clip
    float time = 0, prevTime = 0, speed = 1, duration = 0; // blend states: normalised time, duration 1
    bool loop = true, blend = false, wrapped = false;
    const std::vector<AnimGraphDef::Event>* events = nullptr;
    bool fresh = true;    // events at time 0 fire on the first update
    bool Valid() const { return duration >= 0 && (blend || !clip.empty()); }
  };
  struct FiredEvent { std::string name, state; };
  // A clip to sample for a Play: its time now and at the last update, and its weight in the mix.
  struct ClipSample { std::string clip; float time = 0, prevTime = 0, weight = 0, duration = 0; bool loop = true, wrapped = false; };

  // def may be null; machine: which of def's machines; params: shared with the other layers (null = own)
  void Init(const AnimGraphDef* def, DurationFn duration, int machine = 0, std::shared_ptr<Params> params = nullptr);
  void Start(const std::string& stateOrClip);              // "" = the entry state / nothing
  // Jumps to a state (or a clip by name) over `fade` seconds (0 = at once). false: no such state or clip.
  bool PlayState(const std::string& name, float fade);
  bool HasState(const std::string& name) const;
  void SetFloat(const std::string& name, float v) { (*params_)[name] = v; }
  void SetTrigger(const std::string& name) { (*params_)[name] = 1.f; }
  float Get(const std::string& name) const;
  // Advances by dt (scaled by `speed`), runs transitions and collects the events crossed.
  void Update(float dt, float speed, std::vector<FiredEvent>& events);
  // What to sample for one of Current() / Next(): clip states give one clip, blend states one per sample.
  void Samples(const Play& p, std::vector<ClipSample>& out) const;

  const Play& Current() const { return cur_; }
  const Play& Next() const { return next_; }
  bool Fading() const { return fading_; }
  float FadeWeight() const { return fading_ && fadeTime_ > 0 ? std::min(fade_ / fadeTime_, 1.f) : 0.f; } // weight of Next()
  std::string StateName() const;          // the state being played or faded into
  const Params& ParamsMap() const { return *params_; }
  const AnimGraphDef* Graph() const { return def_; }
  const AnimGraphDef::Machine* Machine() const;
  int transitionsTaken = 0;               // test hook

private:
  const AnimGraphDef* def_ = nullptr;
  int machine_ = 0;
  DurationFn duration_;
  std::shared_ptr<Params> params_ = std::make_shared<Params>();
  Play cur_, next_;
  bool fading_ = false;
  float fade_ = 0, fadeTime_ = 0;
  bool MakePlay(const std::string& name, Play& p) const;
  bool CondTrue(const AnimGraphDef::Transition& t) const;
  void Consume(const AnimGraphDef::Transition& t);
  void Begin(Play&& p, float fade);
  void Advance(Play& p, float dt, std::vector<FiredEvent>& events);
  float BlendDuration(const Play& p) const; // weighted clip length of a blend state now
};
}
