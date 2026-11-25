#pragma once
// Animation graph, first part (queue 7-1, Docs/features/animation.md "업그레이드: 텍스트 저장"): the `.animgraph` text
// asset with variables and one state machine (states play a clip of the skeleton's model; transitions on variables,
// triggers and exit time, each with its cross-fade), and the runtime that plays it for one Animator.
//
//   {"v":1, "skeleton":"Models/Rig.gltf",
//    "vars":{"Speed":"float", "Grounded":"bool", "Jump":"trigger"},
//    "stateMachines":{"Locomotion":{
//      "entry":"Idle",
//      "states":{"Idle":{"play":"Idle"},
//                "Run":{"play":"Run", "speed":1.2, "events":[{"event":"Footstep","at":0.25}]},
//                "Jump":{"play":"Jump", "loop":false}},
//      "transitions":[{"from":"Idle", "to":"Run", "when":"Speed > 0.1", "blend":0.2},
//                     {"from":"*", "to":"Jump", "when":"Jump", "blend":0.1},
//                     {"from":"Jump", "to":"Idle", "exitTime":0.9, "blend":0.15}]}}}
//
// `when`: `var op number` (> >= < <= == !=), a bare bool / trigger name, `!`, `&&`, `||` (no parentheses; && binds
// tighter). A trigger is consumed by the transition that fires on it. `exitTime`: the source state's normalised time
// (time / clip length) the transition waits for. Events: clip times in seconds; they reach C# as OnAnimationEvent.
// Left for later (7-1b and on): blend spaces, several state machines / layers, the pose pipeline, IK, graph functions.
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace bdg {
struct AnimGraphDef {
  enum class VarType { Float, Int, Bool, Trigger };
  struct Var { std::string name; VarType type = VarType::Float; };
  struct Event { std::string name; float at = 0; };
  struct State {
    std::string name, clip; // clip: a clip name of the skeleton's model ("" = the state's name)
    bool loop = true;
    float speed = 1.f;
    std::vector<Event> events;
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
  std::string skeleton, machine = "Base";
  std::vector<Var> vars;
  std::vector<State> states;
  std::vector<Transition> transitions;
  int entry = 0;
  int FindState(std::string_view name) const;
  const Var* FindVar(std::string_view name) const;
};

bool ParseAnimGraph(std::string_view json, AnimGraphDef& out, std::string* error = nullptr);
bool ParseAnimCondition(std::string_view text, std::vector<std::vector<AnimGraphDef::Cond>>& out, std::string* error = nullptr);
std::string WriteAnimGraph(const AnimGraphDef& def); // the same layout, one state / transition per line

// One Animator's playback: the graph (or none: clips played by name) and its variables.
class AnimatorRuntime {
public:
  using DurationFn = std::function<float(const std::string& clip)>; // clip length in seconds, < 0 = no such clip
  struct Play {
    int state = -1;       // graph state, -1 = a clip played directly
    std::string clip;
    float time = 0, prevTime = 0, speed = 1, duration = 0;
    bool loop = true;
    const std::vector<AnimGraphDef::Event>* events = nullptr;
    bool fresh = true;    // events at time 0 fire on the first update
    bool Valid() const { return duration >= 0 && !clip.empty(); }
  };
  struct FiredEvent { std::string name, state; };

  void Init(const AnimGraphDef* def, DurationFn duration); // def may be null; starts nothing yet
  void Start(const std::string& stateOrClip);              // "" = the entry state / nothing
  // Jumps to a state (or a clip by name) over `fade` seconds (0 = at once). false: no such state or clip.
  bool PlayState(const std::string& name, float fade);
  void SetFloat(const std::string& name, float v) { params_[name] = v; }
  void SetTrigger(const std::string& name) { params_[name] = 1.f; }
  float Get(const std::string& name) const;
  // Advances by dt (scaled by `speed`), runs transitions and collects the events crossed.
  void Update(float dt, float speed, std::vector<FiredEvent>& events);

  const Play& Current() const { return cur_; }
  const Play& Next() const { return next_; }
  bool Fading() const { return fading_; }
  float FadeWeight() const { return fading_ && fadeTime_ > 0 ? std::min(fade_ / fadeTime_, 1.f) : 0.f; } // weight of Next()
  std::string StateName() const;          // the state being played or faded into
  const std::map<std::string, float>& Params() const { return params_; }
  const AnimGraphDef* Graph() const { return def_; }
  int transitionsTaken = 0;               // test hook

private:
  const AnimGraphDef* def_ = nullptr;
  DurationFn duration_;
  std::map<std::string, float> params_;
  Play cur_, next_;
  bool fading_ = false;
  float fade_ = 0, fadeTime_ = 0;
  bool MakePlay(const std::string& name, Play& p) const;
  bool CondTrue(const AnimGraphDef::Transition& t) const;
  void Consume(const AnimGraphDef::Transition& t);
  void Begin(Play&& p, float fade);
  static void Advance(Play& p, float dt, std::vector<FiredEvent>& events, const std::string& stateName);
};
}
