#include "Runtime/ProjectSettingsPanel.h"
#include "Core/Log.h"
#include "Runtime/EditorModel.h"
#include "UI/UI.h"
#include <cstdio>
#include <format>
#include <map>

namespace bdg {

namespace {
// Korean labels for the panel; a field without one shows its name spaced out ("frameLimit" -> "Frame Limit").
const std::map<std::string, std::string>& Labels() {
  static const std::map<std::string, std::string> m = {
      {"description", "설명"}, {"maps", "맵 & 모드"}, {"packaging", "패키징"}, {"platforms", "지원 플랫폼"},
      {"display", "화면·창"}, {"physics", "물리"}, {"localization", "언어"}, {"audio", "오디오"},
      {"scripting", "스크립팅"}, {"saves", "저장·개인정보"},
      {"name", "게임 이름"}, {"version", "버전"}, {"buildNumber", "빌드 번호"}, {"company", "회사"}, {"author", "제작자"},
      {"copyright", "저작권"}, {"homepage", "홈페이지"}, {"supportEmail", "문의 이메일"}, {"identifier", "식별자"},
      {"icon", "아이콘"}, {"splash", "시작 화면 이미지"}, {"gameStartScene", "게임 시작 장면"}, {"editorStartScene", "에디터 시작 장면"},
      {"scenesInBuild", "빌드에 넣을 장면"}, {"config", "빌드 구성"}, {"outputFolder", "출력 폴더"}, {"executableName", "실행 파일 이름"},
      {"compressAssets", "에셋 압축"}, {"stripUnusedAssets", "안 쓰는 에셋 빼기"}, {"excludeEditorContent", "에디터 전용 제외"},
      {"debugSymbols", "디버그 심볼"}, {"consoleInShipping", "출시 빌드 콘솔"}, {"zipBuild", "zip으로 묶기"},
      {"explainFailures", "실패 이유 설명"}, {"windowMode", "창 모드"}, {"width", "너비"}, {"height", "높이"},
      {"resizable", "크기 조절"}, {"vsync", "수직 동기화"}, {"frameLimit", "프레임 제한"}, {"runInBackground", "백그라운드 실행"},
      {"showCursor", "마우스 커서"}, {"startQuality", "시작 품질"}, {"gravity", "중력 (3D)"}, {"gravity2D", "중력 (2D)"},
      {"fixedTimestep", "고정 스텝 (초)"}, {"maxFixedSteps", "최대 고정 스텝"}, {"defaultLanguage", "기본 언어"},
      {"languages", "지원 언어"}, {"useSystemLanguage", "시스템 언어 따르기"}, {"sampleRate", "샘플레이트"},
      {"masterVolume", "마스터 볼륨"}, {"playInBackground", "백그라운드 소리"}, {"hotReload", "핫 리로드"},
      {"warningsAsErrors", "경고를 오류로"}, {"saveFolder", "세이브 폴더"}, {"cloudSaves", "클라우드 세이브"},
      {"crashReports", "크래시 보고"}, {"analytics", "사용 통계"}, {"ageRating", "연령 등급"},
  };
  return m;
}
std::string Label(const std::string& key) {
  if (auto it = Labels().find(key); it != Labels().end()) return it->second;
  std::string out;
  for (size_t i = 0; i < key.size(); ++i) {
    char c = key[i];
    if (i == 0) c = (char)std::toupper((unsigned char)c);
    else if (std::isupper((unsigned char)c)) out += ' ';
    out += c;
  }
  return out;
}
} // namespace

void ProjectSettingsPanel::Build(Widget& parent, UIContext& ui, ProjectSettings& s, std::function<void()> changed,
                                 std::function<void()> save) {
  parent.children.clear();
  auto& head = parent.Add(MakeBox(Axis::Row, 8));
  head.padL = 10; head.padR = 10; head.padT = 4; head.padB = 2;
  head.crossAlign = Align::Center;
  head.Add(MakeLabel("프로젝트 설정")).variant = 2;
  auto& badge = head.Add(MakeBadge("● 저장 안 됨", 2));
  badge.id = "ps.dirty";
  badge.hidden = true;
  head.Add(MakeBox(Axis::Row)).width = Length::Fill();
  auto& saveBtn = head.Add(MakeButton("ps.save", "저장")); // primary (accent) only while unsaved: Refresh
  saveBtn.tooltip = "ProjectSettings/Project.json에 저장 (기본값과 다른 값만)";
  saveBtn.onClick = [save](Widget&) { if (save) save(); };
  // UI review B8 (U17): a search box: the rows whose name, key or help matches stay, their cards open
  auto& searchRow = parent.Add(MakeBox(Axis::Row, 8));
  searchRow.padL = 10; searchRow.padR = 10; searchRow.padB = 2;
  auto& search = searchRow.Add(MakeField("ps.search", "", ""));
  search.placeholder = "검색…";
  search.width = Length::Fill();
  search.tooltip = "설정 찾기: 이름(중력), 키(physics.gravity) 또는 설명";
  Widget* panel = &parent;
  search.onEdit = search.onCommit = [panel](Widget& w) { Filter(*panel, w.text2); };
  auto& issues = parent.Add(MakeBox(Axis::Column, 2));
  issues.id = "ps.issues";
  issues.padL = 12;

  const TypeInfo& top = ProjectSettings::StaticType();
  for (const FieldInfo& cat : top.fields) {
    const TypeInfo* ct = cat.value.structType;
    if (!ct) continue;
    auto& card = parent.Add(MakeCard("ps." + cat.name, Label(cat.name)));
    card.collapsed = cat.name != "description"; // the first one open, the rest one click away
    for (const FieldInfo& f : ct->fields) {
      if (!f.meta.header.empty()) card.Add(MakeLabel(f.meta.header)).variant = 3;
      const std::string path = cat.name + "." + f.name, id = "ps." + path;
      std::string json;
      Registry::Get(&s, top, path, json);
      const std::string shown = EditorModel::ToDisplay(json, f.value.type);
      std::unique_ptr<Widget> made;
      if (f.value.type == FieldType::Enum && f.value.enumType) {
        std::vector<std::string> names;
        for (auto& [nm, v] : f.value.enumType->values) names.push_back(nm);
        made = MakeChoice(id, Label(f.name), std::move(names), shown);
      } else if (f.value.type == FieldType::Bool) {
        made = MakeToggle(id, Label(f.name), shown == "true");
      } else {
        made = MakeField(id, Label(f.name), shown);
        const bool numeric = f.value.type == FieldType::Int32 || f.value.type == FieldType::Float || f.value.type == FieldType::Double;
        made->scrub = numeric;
        made->sliderInteger = f.value.type == FieldType::Int32;
        made->axes = f.value.type == FieldType::Vec2 ? 2 : f.value.type == FieldType::Vec3 ? 3 : 0;
        if (numeric && f.meta.hasRange) {
          made->slider = true;
          made->sliderMin = f.meta.rangeMin;
          made->sliderMax = f.meta.rangeMax;
        }
      }
      made->tooltip = f.meta.tooltip.empty() ? path : f.meta.tooltip + "\n" + path; // the key, as in `bdg config`
      const FieldType type = f.value.type;
      auto apply = [&s, path, type, changed](Widget& w) {
        std::string err;
        if (Registry::Set(&s, ProjectSettings::StaticType(), path, EditorModel::FromDisplay(w.text2, type), &err)) {
          if (changed) changed();
          return;
        }
        LogWarn("project settings: {}", err);
        std::string back; // show what is really stored
        Registry::Get(&s, ProjectSettings::StaticType(), path, back);
        w.text2 = EditorModel::ToDisplay(back, type);
      };
      made->onChange = apply;
      made->onCommit = apply;
      card.Add(std::move(made));
    }
  }
  ui.ForgetRemovedWidgets();
}

void ProjectSettingsPanel::Filter(Widget& parent, const std::string& query) {
  const bool searching = query.find_first_not_of(' ') != std::string::npos;
  for (auto& card : parent.children) {
    if (card->kind != WidgetKind::Card || card->id.rfind("ps.", 0) != 0) continue;
    if (!searching) { // back as built: every row, the first card open
      for (auto& row : card->children) row->hidden = false;
      card->hidden = false;
      card->collapsed = card->id != "ps.description";
      continue;
    }
    bool any = false;
    for (auto& row : card->children) {
      const bool isRow = row->id.rfind("ps.", 0) == 0;
      const bool hit = isRow && EditorModel::SearchMatches(query, {row->text, row->id.substr(3), row->tooltip});
      row->hidden = !hit; // captions hide while searching
      any = any || hit;
    }
    card->hidden = !any;
    card->collapsed = !any;
  }
}

void ProjectSettingsPanel::Refresh(UIContext& ui, const ProjectSettings& s, bool dirty) {
  if (Widget* b = ui.Find("ps.dirty")) b->hidden = !dirty;
  if (Widget* b = ui.Find("ps.save")) { // one primary button per screen: Save only when there is something to save
    b->primary = dirty;
    b->disabled = !dirty;
    b->tooltip = dirty ? "ProjectSettings/Project.json에 저장 (기본값과 다른 값만)" : "바뀐 것이 없습니다";
  }
  Widget* box = ui.Find("ps.issues");
  if (!box) return;
  const std::vector<std::string> issues = ValidateProjectSettings(s);
  std::vector<std::string> now;
  for (auto& c : box->children) now.push_back(c->text);
  std::vector<std::string> want;
  for (auto& i : issues) want.push_back("⚠  " + i);
  if (now == want) return;
  box->children.clear();
  for (auto& w : want) {
    auto& l = box->Add(MakeLabel(w));
    l.variant = 3;
  }
  ui.ForgetRemovedWidgets();
}

} // namespace bdg
