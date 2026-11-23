// Windows UI Automation provider for the widget tree. The window is the fragment root; each visible focusable
// widget is a child element (flat list in Tab order). Elements refer to widgets by id, never by pointer:
// panels are rebuilt often, and a stale element must report "not available" instead of crashing.
#include "Platform/Accessibility.h"
#include "Core/Log.h"
#include "Platform/Window.h"
#include "UI/UI.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <uiautomation.h>
#include <atomic>
#include <format>
#include <thread>
#include <string>
#include <vector>

namespace bdg::a11y {
namespace {
HWND g_hwnd = nullptr;
std::function<UIContext*()> g_ui;
class Root;
Root* g_root = nullptr;

std::wstring Wide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
std::string Utf8(const wchar_t* w) {
  if (!w) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s((size_t)(n > 0 ? n - 1 : 0), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

// Key of a widget in the focus order: its id, or "#<index>" for widgets without one.
std::string KeyOf(const std::vector<Widget*>& order, Widget* w) {
  if (!w->id.empty()) return w->id;
  for (size_t i = 0; i < order.size(); ++i) if (order[i] == w) return "#" + std::to_string(i);
  return {};
}
Widget* Lookup(const std::string& key, std::vector<Widget*>* orderOut = nullptr, size_t* indexOut = nullptr) {
  UIContext* ui = g_ui ? g_ui() : nullptr;
  if (!ui) return nullptr;
  std::vector<Widget*> order = ui->FocusOrder();
  for (size_t i = 0; i < order.size(); ++i)
    if (KeyOf(order, order[i]) == key) {
      if (orderOut) *orderOut = std::move(order);
      if (indexOut) *indexOut = i;
      return (*(orderOut ? orderOut : &order))[i];
    }
  return nullptr;
}

long ControlType(const Widget& w) {
  switch (w.kind) {
    case WidgetKind::Button: return UIA_ButtonControlTypeId;
    case WidgetKind::Tabs: return UIA_TabItemControlTypeId;
    case WidgetKind::Field: return UIA_EditControlTypeId;
    case WidgetKind::ListRow: return UIA_ListItemControlTypeId;
    case WidgetKind::Card: case WidgetKind::Node: return UIA_GroupControlTypeId;
    default: return UIA_TextControlTypeId;
  }
}

UiaRect ScreenRect(const Widget& w) {
  UIContext* ui = g_ui ? g_ui() : nullptr;
  float s = ui ? ui->Scale() : 1.f;
  POINT p{(LONG)(w.x * s), (LONG)(w.y * s)};
  ClientToScreen(g_hwnd, &p);
  return {(double)p.x, (double)p.y, (double)(w.w * s), (double)(w.h * s)};
}

template <class T> struct ComBase {
  std::atomic<ULONG> refs{1};
  ULONG AddRefImpl() { return ++refs; }
  ULONG ReleaseImpl(T* self) { ULONG r = --refs; if (!r) delete self; return r; }
};

// ---- One widget -------------------------------------------------------------------------------
class Element : public IRawElementProviderSimple, public IRawElementProviderFragment, public IInvokeProvider,
                public IValueProvider, ComBase<Element> {
public:
  explicit Element(std::string key) : key_(std::move(key)) {}
  virtual ~Element() = default;

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
    else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
    else if (riid == __uuidof(IInvokeProvider)) *out = static_cast<IInvokeProvider*>(this);
    else if (riid == __uuidof(IValueProvider)) *out = static_cast<IValueProvider*>(this);
    else { *out = nullptr; return E_NOINTERFACE; }
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return AddRefImpl(); }
  ULONG STDMETHODCALLTYPE Release() override { return ReleaseImpl(this); }

  // IRawElementProviderSimple
  HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* o) override {
    *o = (ProviderOptions)(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** out) override {
    *out = nullptr;
    Widget* w = Lookup(key_);
    if (!w) return UIA_E_ELEMENTNOTAVAILABLE;
    if (id == UIA_ValuePatternId && w->kind == WidgetKind::Field) *out = static_cast<IValueProvider*>(this);
    else if (id == UIA_InvokePatternId && w->kind != WidgetKind::Field) *out = static_cast<IInvokeProvider*>(this);
    if (*out) AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* v) override;
  HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override { *out = nullptr; return S_OK; }

  // IRawElementProviderFragment
  HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection dir, IRawElementProviderFragment** out) override;
  HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
    int id[2] = {UiaAppendRuntimeId, (int)std::hash<std::string>{}(key_)};
    *out = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!*out) return E_OUTOFMEMORY;
    for (LONG i = 0; i < 2; ++i) SafeArrayPutElement(*out, &i, &id[i]);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* r) override {
    Widget* w = Lookup(key_);
    if (!w) return UIA_E_ELEMENTNOTAVAILABLE;
    *r = ScreenRect(*w);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override { *out = nullptr; return S_OK; }
  HRESULT STDMETHODCALLTYPE SetFocus() override {
    Widget* w = Lookup(key_);
    if (!w) return UIA_E_ELEMENTNOTAVAILABLE;
    g_ui()->Focus(w);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override;

  // IInvokeProvider
  HRESULT STDMETHODCALLTYPE Invoke() override {
    Widget* w = Lookup(key_);
    if (!w) return UIA_E_ELEMENTNOTAVAILABLE;
    g_ui()->Invoke(*w);
    return S_OK;
  }
  // IValueProvider (text boxes)
  HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {
    Widget* w = Lookup(key_);
    if (!w || w->kind != WidgetKind::Field) return UIA_E_ELEMENTNOTAVAILABLE;
    w->text2 = Utf8(value);
    w->cursor = w->anchor = w->text2.size();
    if (w->onCommit) w->onCommit(*w); // a screen reader edit is committed like Enter (goes through commands)
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override {
    Widget* w = Lookup(key_);
    if (!w) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = SysAllocString(Wide(w->text2).c_str());
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override { *out = FALSE; return S_OK; }

  const std::string& Key() const { return key_; }

private:
  std::string key_;
};

// ---- The window ---------------------------------------------------------------------------------
class Root : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot,
             ComBase<Root> {
public:
  virtual ~Root() = default;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
    else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
    else if (riid == __uuidof(IRawElementProviderFragmentRoot)) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
    else { *out = nullptr; return E_NOINTERFACE; }
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return AddRefImpl(); }
  ULONG STDMETHODCALLTYPE Release() override { return ReleaseImpl(this); }

  HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* o) override {
    *o = (ProviderOptions)(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** out) override { *out = nullptr; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* v) override {
    v->vt = VT_EMPTY;
    if (id == UIA_NamePropertyId) { v->vt = VT_BSTR; v->bstrVal = SysAllocString(L"Bidulgi Engine - Editor"); }
    else if (id == UIA_ControlTypePropertyId) { v->vt = VT_I4; v->lVal = UIA_WindowControlTypeId; }
    else if (id == UIA_FrameworkIdPropertyId) { v->vt = VT_BSTR; v->bstrVal = SysAllocString(L"Bidulgi"); }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
    return UiaHostProviderFromHwnd(g_hwnd, out);
  }
  HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection dir, IRawElementProviderFragment** out) override {
    *out = nullptr;
    if (dir != NavigateDirection_FirstChild && dir != NavigateDirection_LastChild) return S_OK;
    UIContext* ui = g_ui ? g_ui() : nullptr;
    if (!ui) return S_OK;
    auto order = ui->FocusOrder();
    if (order.empty()) return S_OK;
    Widget* w = dir == NavigateDirection_FirstChild ? order.front() : order.back();
    *out = new Element(KeyOf(order, w));
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override { *out = nullptr; return S_OK; } // host provides it
  HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* r) override { *r = {}; return S_OK; }    // host provides it
  HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override { *out = nullptr; return S_OK; }
  HRESULT STDMETHODCALLTYPE SetFocus() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
    *out = this;
    AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override {
    *out = nullptr;
    UIContext* ui = g_ui ? g_ui() : nullptr;
    if (!ui) return S_OK;
    auto order = ui->FocusOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      UiaRect r = ScreenRect(**it);
      if (x >= r.left && y >= r.top && x < r.left + r.width && y < r.top + r.height) { *out = new Element(KeyOf(order, *it)); break; }
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override {
    *out = nullptr;
    UIContext* ui = g_ui ? g_ui() : nullptr;
    if (ui && ui->Focused()) *out = new Element(KeyOf(ui->FocusOrder(), ui->Focused()));
    return S_OK;
  }
};

HRESULT Element::GetPropertyValue(PROPERTYID id, VARIANT* v) {
  v->vt = VT_EMPTY;
  Widget* w = Lookup(key_);
  if (!w) return UIA_E_ELEMENTNOTAVAILABLE;
  UIContext* ui = g_ui();
  auto str = [&](const std::string& s) { v->vt = VT_BSTR; v->bstrVal = SysAllocString(Wide(s).c_str()); };
  auto boolean = [&](bool b) { v->vt = VT_BOOL; v->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE; };
  switch (id) {
    case UIA_NamePropertyId: str(UIContext::AccessibleName(*w)); break;
    case UIA_ControlTypePropertyId: v->vt = VT_I4; v->lVal = ControlType(*w); break;
    case UIA_AutomationIdPropertyId: str(w->id); break;
    case UIA_FrameworkIdPropertyId: str("Bidulgi"); break;
    case UIA_IsKeyboardFocusablePropertyId: boolean(true); break;
    case UIA_HasKeyboardFocusPropertyId: boolean(ui->Focused() == w); break;
    case UIA_IsEnabledPropertyId: case UIA_IsControlElementPropertyId: case UIA_IsContentElementPropertyId: boolean(true); break;
    case UIA_IsOffscreenPropertyId: boolean(false); break;
    case UIA_SelectionItemIsSelectedPropertyId: boolean(w->selected); break;
    default: break;
  }
  return S_OK;
}

HRESULT Element::Navigate(NavigateDirection dir, IRawElementProviderFragment** out) {
  *out = nullptr;
  if (dir == NavigateDirection_Parent) { *out = g_root; g_root->AddRef(); return S_OK; }
  if (dir != NavigateDirection_NextSibling && dir != NavigateDirection_PreviousSibling) return S_OK;
  std::vector<Widget*> order;
  size_t i = 0;
  if (!Lookup(key_, &order, &i)) return UIA_E_ELEMENTNOTAVAILABLE;
  size_t j = dir == NavigateDirection_NextSibling ? i + 1 : i - 1;
  if (dir == NavigateDirection_PreviousSibling && i == 0) return S_OK;
  if (j < order.size()) *out = new Element(KeyOf(order, order[j]));
  return S_OK;
}

HRESULT Element::get_FragmentRoot(IRawElementProviderFragmentRoot** out) {
  *out = g_root;
  g_root->AddRef();
  return S_OK;
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
  if (msg == WM_GETOBJECT && (long)lp == UiaRootObjectId && g_root) return UiaReturnRawElementProvider(hwnd, wp, lp, g_root);
  if (msg == WM_DESTROY) { UiaReturnRawElementProvider(hwnd, 0, 0, nullptr); Detach(); }
  return DefSubclassProc(hwnd, msg, wp, lp);
}
}

bool Attach(Window& window, std::function<UIContext*()> currentUi) {
  g_hwnd = (HWND)window.NativeHandle();
  if (!g_hwnd) return false;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // UIA calls arrive through COM on this (UI) thread
  g_ui = std::move(currentUi);
  g_root = new Root();
  if (!SetWindowSubclass(g_hwnd, Proc, 1, 0)) { g_root->Release(); g_root = nullptr; return false; }
  LogInfo("a11y: UI Automation provider attached");
  return true;
}

void FocusChanged() {
  if (!g_root || !UiaClientsAreListening()) return;
  UIContext* ui = g_ui ? g_ui() : nullptr;
  if (!ui || !ui->Focused()) return;
  Element* e = new Element(KeyOf(ui->FocusOrder(), ui->Focused()));
  UiaRaiseAutomationEvent(e, UIA_AutomationFocusChangedEventId);
  e->Release();
}

namespace {
std::thread g_probe;
std::atomic<bool> g_probeDone{false};
std::string g_probeResult;

const char* ControlName(CONTROLTYPEID id) {
  switch (id) {
    case UIA_ButtonControlTypeId: return "Button";
    case UIA_EditControlTypeId: return "Edit";
    case UIA_TabItemControlTypeId: return "TabItem";
    case UIA_ListItemControlTypeId: return "ListItem";
    case UIA_GroupControlTypeId: return "Group";
    case UIA_TextControlTypeId: return "Text";
    case UIA_WindowControlTypeId: return "Window";
    default: return "Other";
  }
}
}

void StartProbe() {
  if (g_probe.joinable()) return;
  g_probeDone = false;
  HWND hwnd = g_hwnd;
  g_probe = std::thread([hwnd] {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string result = "probe failed: no UI Automation";
    IUIAutomation* uia = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&uia))) {
      std::string name = "-", type = "-";
      int children = 0;
      IUIAutomationElement* window = nullptr;
      if (SUCCEEDED(uia->ElementFromHandle(hwnd, &window)) && window) {
        IUIAutomationCondition* all = nullptr;
        uia->CreateTrueCondition(&all);
        IUIAutomationElementArray* arr = nullptr;
        if (all && SUCCEEDED(window->FindAll(TreeScope_Children, all, &arr)) && arr) { arr->get_Length(&children); arr->Release(); }
        // The element with keyboard focus inside this window (does not depend on the window being in front).
        VARIANT yes;
        yes.vt = VT_BOOL;
        yes.boolVal = VARIANT_TRUE;
        IUIAutomationCondition* focused = nullptr;
        uia->CreatePropertyCondition(UIA_HasKeyboardFocusPropertyId, yes, &focused);
        IUIAutomationElement* f = nullptr;
        if (focused && SUCCEEDED(window->FindFirst(TreeScope_Children, focused, &f)) && f) {
          BSTR n = nullptr;
          if (SUCCEEDED(f->get_CurrentName(&n)) && n) { name = Utf8(n); SysFreeString(n); }
          CONTROLTYPEID ct = 0;
          if (SUCCEEDED(f->get_CurrentControlType(&ct))) type = ControlName(ct);
          f->Release();
        }
        if (focused) focused->Release();
        if (all) all->Release();
        window->Release();
      }
      result = std::format("focused_name={} control={} children={}", name, type, children);
      uia->Release();
    }
    g_probeResult = result;
    CoUninitialize();
    g_probeDone = true;
  });
}

bool ProbeDone() { return g_probeDone; }

std::string ProbeResult() {
  if (g_probe.joinable()) g_probe.join();
  return g_probeResult;
}

void Detach() {
  if (g_probe.joinable()) g_probe.join();
  if (!g_root) return;
  RemoveWindowSubclass(g_hwnd, Proc, 1);
  UiaDisconnectProvider(g_root);
  g_root->Release();
  g_root = nullptr;
  g_ui = nullptr;
}
}
