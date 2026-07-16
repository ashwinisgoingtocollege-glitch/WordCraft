#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "editor.h"
#include "rendereditor.h"

#include <ole2.h>
#include <richole.h>
#include <textserv.h>
#include <d2d1.h>
#include <imm.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <new>
#include <vector>

namespace {

typedef HRESULT (STDAPICALLTYPE *CreateTextServicesFn)(
    IUnknown *, ITextHost *, IUnknown **);
typedef HRESULT (STDAPICALLTYPE *ShutdownTextServicesFn)(IUnknown *);

struct RenderRuntime {
    HINSTANCE instance;
    HMODULE richEditModule;
    CreateTextServicesFn createTextServices;
    ShutdownTextServicesFn shutdownTextServices;
    IID iidTextServices;
    IID iidTextServices2;
    IID iidTextHost;
    IID iidTextHost2;
    BOOL hasTextServices2;
    BOOL registered;
};

static RenderRuntime runtime = {};

static BOOL copy_exported_iid(HMODULE module, const char *name, IID *iid)
{
    const IID *exported;

    if (module == nullptr || name == nullptr || iid == nullptr) {
        return FALSE;
    }
    exported = reinterpret_cast<const IID *>(
        reinterpret_cast<const void *>(GetProcAddress(module, name)));
    if (exported == nullptr) {
        std::memset(iid, 0, sizeof(*iid));
        return FALSE;
    }
    *iid = *exported;
    return TRUE;
}

static BOOL environment_flag_enabled(const WCHAR *name)
{
    WCHAR value[16];
    DWORD length;

    if (name == nullptr) {
        return FALSE;
    }
    length = GetEnvironmentVariableW(name, value, ARRAYSIZE(value));
    if (length == 0 || length >= ARRAYSIZE(value)) {
        return FALSE;
    }
    return lstrcmpiW(value, L"1") == 0 ||
           lstrcmpiW(value, L"true") == 0 ||
           lstrcmpiW(value, L"yes") == 0 ||
           lstrcmpiW(value, L"on") == 0;
}

static D2D1_COLOR_F color_from_colorref(COLORREF color)
{
    D2D1_COLOR_F result;
    result.r = static_cast<FLOAT>(GetRValue(color)) / 255.0f;
    result.g = static_cast<FLOAT>(GetGValue(color)) / 255.0f;
    result.b = static_cast<FLOAT>(GetBValue(color)) / 255.0f;
    result.a = 1.0f;
    return result;
}

static LRESULT saturating_counter(ULONGLONG value)
{
    const ULONGLONG maximum = static_cast<ULONGLONG>(LLONG_MAX);
    return value > maximum ? static_cast<LRESULT>(LLONG_MAX)
                           : static_cast<LRESULT>(value);
}

struct RtfTransfer {
    std::vector<BYTE> *bytes;
    SIZE_T offset;
};

static DWORD CALLBACK stream_rtf_out(DWORD_PTR cookie, LPBYTE buffer,
                                     LONG byteCount, LONG *transferred)
{
    RtfTransfer *transfer = reinterpret_cast<RtfTransfer *>(cookie);

    if (transfer == nullptr || transfer->bytes == nullptr ||
        transferred == nullptr || byteCount < 0 ||
        (byteCount > 0 && buffer == nullptr)) {
        return ERROR_INVALID_PARAMETER;
    }
    *transferred = 0;
    if (byteCount == 0) {
        return ERROR_SUCCESS;
    }
    try {
        transfer->bytes->insert(transfer->bytes->end(), buffer,
                                buffer + byteCount);
    } catch (...) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    *transferred = byteCount;
    return ERROR_SUCCESS;
}

static DWORD CALLBACK stream_rtf_in(DWORD_PTR cookie, LPBYTE buffer,
                                    LONG byteCount, LONG *transferred)
{
    RtfTransfer *transfer = reinterpret_cast<RtfTransfer *>(cookie);
    SIZE_T remaining;
    SIZE_T copied;

    if (transfer == nullptr || transfer->bytes == nullptr ||
        transferred == nullptr || byteCount < 0 ||
        (byteCount > 0 && buffer == nullptr) ||
        transfer->offset > transfer->bytes->size()) {
        return ERROR_INVALID_PARAMETER;
    }
    remaining = transfer->bytes->size() - transfer->offset;
    copied = std::min<SIZE_T>(remaining, static_cast<SIZE_T>(byteCount));
    if (copied > 0) {
        std::memcpy(buffer, transfer->bytes->data() + transfer->offset,
                    copied);
        transfer->offset += copied;
    }
    *transferred = static_cast<LONG>(copied);
    return ERROR_SUCCESS;
}

static BOOL message_affects_formatter(UINT message)
{
    switch (message) {
    case WM_SETTEXT:
    case WM_CHAR:
    case WM_UNICHAR:
    case WM_CUT:
    case WM_PASTE:
    case WM_CLEAR:
    case WM_UNDO:
    case EM_UNDO:
    case WM_IME_COMPOSITION:
    case EM_REPLACESEL:
    case EM_STREAMIN:
    case EM_SETCHARFORMAT:
    case EM_SETPARAFORMAT:
    case EM_SETTEXTEX:
    case EM_REDO:
        return TRUE;
    default:
        return FALSE;
    }
}

class RenderEditor final : public ITextHost2 {
public:
    explicit RenderEditor(HWND window)
        : references_(1), window_(window), privateUnknown_(nullptr),
          services_(nullptr), services2_(nullptr), d2dFactory_(nullptr),
          hwndTarget_(nullptr), formatterWindow_(nullptr),
          formatterDirty_(TRUE), background_(GetSysColor(COLOR_WINDOW)),
          propertyBits_(0), backend_(RENDER_ENGINE_BACKEND_NONE),
          fallbackReason_(RENDER_FALLBACK_NONE), d2dDrawCount_(0),
          gdiDrawCount_(0), lastDrawPath_(RENDER_DRAW_PATH_NONE),
          lastDrawResult_(E_PENDING), targetGeneration_(0),
          d2dFailureCount_(0), redrawEnabled_(TRUE), inPlaceActive_(FALSE),
          uiActive_(FALSE), servicesFreed_(FALSE), activationState_(0),
          selectionMessageCount_(0), lastSelectionStart_(-1),
          lastSelectionResult_(E_PENDING), lastSelectionPage_(-1)
    {
        std::memset(&characterFormat_, 0, sizeof(characterFormat_));
        characterFormat_.cbSize = sizeof(characterFormat_);
        characterFormat_.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR |
                                  CFM_CHARSET;
        characterFormat_.yHeight = WORDCRAFT_DEFAULT_FONT_SIZE_TWIPS;
        characterFormat_.crTextColor = GetSysColor(COLOR_WINDOWTEXT);
        characterFormat_.bCharSet = DEFAULT_CHARSET;
        StringCchCopyW(characterFormat_.szFaceName,
                       ARRAYSIZE(characterFormat_.szFaceName),
                       WORDCRAFT_DEFAULT_FONT_FACE);

        std::memset(&paragraphFormat_, 0, sizeof(paragraphFormat_));
        paragraphFormat_.cbSize = sizeof(paragraphFormat_);
        paragraphFormat_.dwMask = PFM_ALIGNMENT;
        paragraphFormat_.wAlignment = PFA_LEFT;
    }

    BOOL Initialize(LPCWSTR initialText)
    {
        RECT client;
        HRESULT status;
        BOOL forcedGdi;
        BOOL requestedD2d;

        if (runtime.createTextServices == nullptr) {
            SetLastError(ERROR_PROC_NOT_FOUND);
            return FALSE;
        }

        forcedGdi = environment_flag_enabled(L"WORDCRAFT_DISABLE_D2D");
        requestedD2d = !forcedGdi && runtime.hasTextServices2;
        if (forcedGdi) {
            fallbackReason_ = RENDER_FALLBACK_FORCED;
        } else if (!runtime.hasTextServices2) {
            fallbackReason_ = RENDER_FALLBACK_NO_TEXT_SERVICES2;
        }

        if (requestedD2d) {
            status = D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                IID_ID2D1Factory, nullptr,
                reinterpret_cast<void **>(&d2dFactory_));
            if (FAILED(status) || d2dFactory_ == nullptr) {
                d2dFactory_ = nullptr;
                requestedD2d = FALSE;
                fallbackReason_ = RENDER_FALLBACK_NO_D2D_FACTORY;
                lastDrawResult_ = status;
            }
        }

        propertyBits_ = ComputePropertyBits(requestedD2d);
        status = runtime.createTextServices(nullptr, this, &privateUnknown_);
        if (FAILED(status) || privateUnknown_ == nullptr) {
            lastDrawResult_ = status;
            SetLastError(ERROR_CANNOT_MAKE);
            return FALSE;
        }

        if (requestedD2d) {
            status = privateUnknown_->QueryInterface(
                runtime.iidTextServices2,
                reinterpret_cast<void **>(&services2_));
            if (SUCCEEDED(status) && services2_ != nullptr) {
                services_ = static_cast<ITextServices *>(services2_);
                backend_ = RENDER_ENGINE_BACKEND_DIRECTWRITE;
            } else {
                services2_ = nullptr;
                propertyBits_ = ComputePropertyBits(FALSE);
                fallbackReason_ = RENDER_FALLBACK_NO_TEXT_SERVICES2;
                if (d2dFactory_ != nullptr) {
                    d2dFactory_->Release();
                    d2dFactory_ = nullptr;
                }
            }
        }

        if (services_ == nullptr) {
            status = privateUnknown_->QueryInterface(
                runtime.iidTextServices,
                reinterpret_cast<void **>(&services_));
            if (FAILED(status) || services_ == nullptr) {
                lastDrawResult_ = status;
                SetLastError(ERROR_NOINTERFACE);
                return FALSE;
            }
            backend_ = RENDER_ENGINE_BACKEND_GDI;
        }

        services_->OnTxPropertyBitsChange(
            TXTBIT_RICHTEXT | TXTBIT_MULTILINE | TXTBIT_READONLY |
                TXTBIT_HIDESELECTION | TXTBIT_SAVESELECTION |
                TXTBIT_WORDWRAP | TXTBIT_ALLOWBEEP |
                TXTBIT_DISABLEDRAG | TXTBIT_D2DDWRITE |
                TXTBIT_D2DSIMPLETYPOGRAPHY | TXTBIT_D2DPIXELSNAPPED |
                TXTBIT_D2DSUBPIXELLINES | TXTBIT_ADVANCEDINPUT,
            propertyBits_);

        GetClientRect(window_, &client);
        status = services_->OnTxInPlaceActivate(&client);
        if (FAILED(status)) {
            lastDrawResult_ = status;
            return FALSE;
        }
        inPlaceActive_ = TRUE;

        if (initialText != nullptr && initialText[0] != L'\0') {
            SendToServices(WM_SETTEXT, 0,
                           reinterpret_cast<LPARAM>(initialText), nullptr);
        }
        lastDrawResult_ = S_OK;
        return TRUE;
    }

    void Shutdown()
    {
        if (services_ != nullptr && uiActive_) {
            services_->OnTxUIDeactivate();
            uiActive_ = FALSE;
        }
        if (services_ != nullptr && inPlaceActive_) {
            services_->OnTxInPlaceDeactivate();
            inPlaceActive_ = FALSE;
        }
        DiscardHwndTarget();
        if (formatterWindow_ != nullptr) {
            DestroyWindow(formatterWindow_);
            formatterWindow_ = nullptr;
        }
        if (d2dFactory_ != nullptr) {
            d2dFactory_->Release();
            d2dFactory_ = nullptr;
        }
        /* Release the QueryInterface reference before consuming the private
         * CreateTextServices reference.  ShutdownTextServices calls Release
         * itself, so that private pointer must never be released again. */
        if (services2_ != nullptr) {
            services2_->Release();
            services2_ = nullptr;
            services_ = nullptr;
        } else if (services_ != nullptr) {
            services_->Release();
            services_ = nullptr;
        }
        if (privateUnknown_ != nullptr) {
            if (!servicesFreed_ &&
                runtime.shutdownTextServices != nullptr) {
                runtime.shutdownTextServices(privateUnknown_);
            } else if (!servicesFreed_) {
                privateUnknown_->Release();
            }
            privateUnknown_ = nullptr;
        }
    }

    HRESULT SendToServices(UINT message, WPARAM wParam, LPARAM lParam,
                           LRESULT *result)
    {
        LRESULT localResult = 0;
        HRESULT status;

        if (services_ == nullptr) {
            return E_UNEXPECTED;
        }
        if (message_affects_formatter(message)) {
            formatterDirty_ = TRUE;
        }
        status = services_->TxSendMessage(message, wParam, lParam,
                                          &localResult);
        if (result != nullptr) {
            *result = localResult;
        }
        return status;
    }

    HRESULT SendFormatRange(WPARAM wParam, LPARAM lParam, LRESULT *result)
    {
        LRESULT localResult = 0;
        if (backend_ != RENDER_ENGINE_BACKEND_DIRECTWRITE) {
            return SendToServices(EM_FORMATRANGE, wParam, lParam, result);
        }
        if (lParam == 0) {
            if (formatterWindow_ != nullptr) {
                localResult = SendMessageW(formatterWindow_,
                                           EM_FORMATRANGE, wParam, 0);
            }
        } else {
            if (!SynchronizeFormatter()) {
                return E_FAIL;
            }
            localResult = SendMessageW(formatterWindow_, EM_FORMATRANGE,
                                       wParam, lParam);
        }
        if (result != nullptr) {
            *result = localResult;
        }
        return S_OK;
    }

    void NoteSelectionMessage(LONG start, HRESULT result)
    {
        LRESULT page = -1;
        ++selectionMessageCount_;
        lastSelectionStart_ = start;
        lastSelectionResult_ = result;
        SendToServices(EM_GETPAGE, 0, 0, &page);
        lastSelectionPage_ = static_cast<LONG>(page);
    }

    LRESULT Paint()
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window_, &paint);
        if (dc != nullptr && redrawEnabled_) {
            PaintToWindow(dc, paint.rcPaint);
        }
        EndPaint(window_, &paint);
        return 0;
    }

    LRESULT PrintClient(HDC dc)
    {
        RECT client;

        if (dc == nullptr || services_ == nullptr) {
            return 0;
        }
        GetClientRect(window_, &client);
        if (IsRectEmpty(&client)) {
            return 0;
        }
        if (backend_ == RENDER_ENGINE_BACKEND_DIRECTWRITE &&
            services2_ != nullptr && d2dFactory_ != nullptr) {
            HRESULT status = PaintD2dToDc(dc, client);
            if (SUCCEEDED(status)) {
                return 1;
            }
        }
        return SUCCEEDED(PaintGdi(dc, client, client)) ? 1 : 0;
    }

    void Resize()
    {
        RECT client;
        D2D1_SIZE_U size;

        GetClientRect(window_, &client);
        if (hwndTarget_ != nullptr) {
            size.width = static_cast<UINT32>(
                std::max<LONG>(1, client.right - client.left));
            size.height = static_cast<UINT32>(
                std::max<LONG>(1, client.bottom - client.top));
            HRESULT status = hwndTarget_->Resize(size);
            if (status == D2DERR_RECREATE_TARGET) {
                DiscardHwndTarget();
            }
        }
        if (services_ != nullptr) {
            services_->OnTxPropertyBitsChange(
                TXTBIT_CLIENTRECTCHANGE | TXTBIT_EXTENTCHANGE,
                TXTBIT_CLIENTRECTCHANGE | TXTBIT_EXTENTCHANGE);
        }
    }

    void StyleChanged()
    {
        DWORD updated = ComputePropertyBits(
            backend_ == RENDER_ENGINE_BACKEND_DIRECTWRITE);
        DWORD changed = propertyBits_ ^ updated;

        propertyBits_ = updated;
        if (services_ != nullptr && changed != 0) {
            services_->OnTxPropertyBitsChange(changed,
                                               propertyBits_ & changed);
        }
    }

    void SetRedraw(BOOL enabled)
    {
        redrawEnabled_ = enabled;
        if (enabled) {
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    void SetBackground(WPARAM useSystem, LPARAM color)
    {
        background_ = useSystem != 0
                          ? GetSysColor(COLOR_WINDOW)
                          : static_cast<COLORREF>(color);
    }

    void ActivateUi()
    {
        if (services_ != nullptr && !uiActive_) {
            if (SUCCEEDED(services_->OnTxUIActivate())) {
                uiActive_ = TRUE;
            }
        }
    }

    void DeactivateUi()
    {
        if (services_ != nullptr && uiActive_) {
            services_->OnTxUIDeactivate();
            uiActive_ = FALSE;
        }
    }

    LRESULT Query(UINT query) const
    {
        switch (query) {
        case WCQ_RENDER_ENGINE_WINDOWLESS:
            return services_ != nullptr;
        case WCQ_RENDER_ENGINE_BACKEND:
            return backend_;
        case WCQ_RENDER_ENGINE_D2D_CAPABLE:
            return services2_ != nullptr && d2dFactory_ != nullptr;
        case WCQ_RENDER_ENGINE_PROPERTY_BITS:
            return static_cast<LRESULT>(propertyBits_);
        case WCQ_RENDER_ENGINE_D2D_DRAW_COUNT:
            return saturating_counter(d2dDrawCount_);
        case WCQ_RENDER_ENGINE_GDI_DRAW_COUNT:
            return saturating_counter(gdiDrawCount_);
        case WCQ_RENDER_ENGINE_LAST_DRAW_PATH:
            return lastDrawPath_;
        case WCQ_RENDER_ENGINE_LAST_DRAW_RESULT:
            return static_cast<LRESULT>(lastDrawResult_);
        case WCQ_RENDER_ENGINE_FALLBACK_REASON:
            return fallbackReason_;
        case WCQ_RENDER_ENGINE_TARGET_GENERATION:
            return targetGeneration_;
        case WCQ_RENDER_ENGINE_SELECTION_MESSAGE_COUNT:
            return saturating_counter(selectionMessageCount_);
        case WCQ_RENDER_ENGINE_LAST_SELECTION_START:
            return lastSelectionStart_;
        case WCQ_RENDER_ENGINE_LAST_SELECTION_RESULT:
            return static_cast<LRESULT>(lastSelectionResult_);
        case WCQ_RENDER_ENGINE_LAST_SELECTION_PAGE:
            return lastSelectionPage_;
        default:
            return 0;
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                             void **object) override
    {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, runtime.iidTextHost) ||
            (runtime.hasTextServices2 &&
             IsEqualIID(iid, runtime.iidTextHost2))) {
            *object = static_cast<ITextHost2 *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG remaining = InterlockedDecrement(&references_);
        if (remaining == 0) {
            delete this;
            return 0;
        }
        return static_cast<ULONG>(remaining);
    }

    HDC TxGetDC() override
    {
        return GetDC(window_);
    }

    INT TxReleaseDC(HDC dc) override
    {
        return ReleaseDC(window_, dc);
    }

    BOOL TxShowScrollBar(INT bar, BOOL show) override
    {
        return ShowScrollBar(window_, bar, show);
    }

    BOOL TxEnableScrollBar(INT flags, INT arrows) override
    {
        return EnableScrollBar(window_, flags, arrows);
    }

    BOOL TxSetScrollRange(INT bar, LONG minimum, INT maximum,
                          BOOL redraw) override
    {
        return SetScrollRange(window_, bar, minimum, maximum, redraw);
    }

    BOOL TxSetScrollPos(INT bar, INT position, BOOL redraw) override
    {
        SetScrollPos(window_, bar, position, redraw);
        return TRUE;
    }

    void TxInvalidateRect(LPCRECT rect, BOOL erase) override
    {
        if (redrawEnabled_) {
            InvalidateRect(window_, rect, erase);
        }
    }

    void TxViewChange(BOOL update) override
    {
        if (!redrawEnabled_) {
            return;
        }
        InvalidateRect(window_, nullptr, FALSE);
        if (update) {
            UpdateWindow(window_);
        }
    }

    BOOL TxCreateCaret(HBITMAP bitmap, INT width, INT height) override
    {
        return CreateCaret(window_, bitmap, std::max(1, width),
                           std::max(1, height));
    }

    BOOL TxShowCaret(BOOL show) override
    {
        return show ? ShowCaret(window_) : HideCaret(window_);
    }

    BOOL TxSetCaretPos(INT x, INT y) override
    {
        return SetCaretPos(x, y);
    }

    BOOL TxSetTimer(UINT identifier, UINT timeout) override
    {
        return SetTimer(window_, identifier, timeout, nullptr) != 0;
    }

    void TxKillTimer(UINT identifier) override
    {
        KillTimer(window_, identifier);
    }

    void TxScrollWindowEx(INT x, INT y, LPCRECT scroll,
                          LPCRECT clip, HRGN updateRegion,
                          LPRECT update, UINT flags) override
    {
        if (backend_ == RENDER_ENGINE_BACKEND_DIRECTWRITE) {
            if (update != nullptr) {
                GetClientRect(window_, update);
            }
            if (updateRegion != nullptr) {
                RECT client;
                GetClientRect(window_, &client);
                SetRectRgn(updateRegion, client.left, client.top,
                           client.right, client.bottom);
            }
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        ScrollWindowEx(window_, x, y, scroll, clip, updateRegion,
                       update, flags);
    }

    void TxSetCapture(BOOL capture) override
    {
        if (capture) {
            SetCapture(window_);
        } else if (GetCapture() == window_) {
            ReleaseCapture();
        }
    }

    void TxSetFocus() override
    {
        SetFocus(window_);
    }

    void TxSetCursor(HCURSOR cursor, BOOL) override
    {
        SetCursor(cursor);
    }

    BOOL TxScreenToClient(LPPOINT point) override
    {
        return ScreenToClient(window_, point);
    }

    BOOL TxClientToScreen(LPPOINT point) override
    {
        return ClientToScreen(window_, point);
    }

    HRESULT TxActivate(LONG *oldState) override
    {
        if (oldState == nullptr) {
            return E_POINTER;
        }
        *oldState = activationState_;
        activationState_ = 1;
        return S_OK;
    }

    HRESULT TxDeactivate(LONG newState) override
    {
        activationState_ = newState;
        return S_OK;
    }

    HRESULT TxGetClientRect(LPRECT rect) override
    {
        return rect != nullptr && GetClientRect(window_, rect)
                   ? S_OK : E_INVALIDARG;
    }

    HRESULT TxGetViewInset(LPRECT inset) override
    {
        if (inset == nullptr) {
            return E_POINTER;
        }
        SetRectEmpty(inset);
        return S_OK;
    }

    HRESULT TxGetCharFormat(const CHARFORMATW **format) override
    {
        if (format == nullptr) {
            return E_POINTER;
        }
        *format = reinterpret_cast<const CHARFORMATW *>(&characterFormat_);
        return S_OK;
    }

    HRESULT TxGetParaFormat(const PARAFORMAT **format) override
    {
        if (format == nullptr) {
            return E_POINTER;
        }
        *format = reinterpret_cast<const PARAFORMAT *>(&paragraphFormat_);
        return S_OK;
    }

    COLORREF TxGetSysColor(int index) override
    {
        if (index == COLOR_WINDOW) {
            return background_;
        }
        if (index == COLOR_WINDOWTEXT) {
            return characterFormat_.crTextColor;
        }
        return GetSysColor(index);
    }

    HRESULT TxGetBackStyle(TXTBACKSTYLE *style) override
    {
        if (style == nullptr) {
            return E_POINTER;
        }
        *style = TXTBACK_OPAQUE;
        return S_OK;
    }

    HRESULT TxGetMaxLength(DWORD *length) override
    {
        if (length == nullptr) {
            return E_POINTER;
        }
        *length = 0x7FFFFFFE;
        return S_OK;
    }

    HRESULT TxGetScrollBars(DWORD *bars) override
    {
        LONG_PTR style;
        if (bars == nullptr) {
            return E_POINTER;
        }
        style = GetWindowLongPtrW(window_, GWL_STYLE);
        *bars = static_cast<DWORD>(style) & (WS_HSCROLL | WS_VSCROLL);
        return S_OK;
    }

    HRESULT TxGetPasswordChar(TCHAR *character) override
    {
        if (character == nullptr) {
            return E_POINTER;
        }
        *character = L'*';
        return S_OK;
    }

    HRESULT TxGetAcceleratorPos(LONG *position) override
    {
        if (position == nullptr) {
            return E_POINTER;
        }
        *position = -1;
        return S_OK;
    }

    HRESULT TxGetExtent(LPSIZEL extent) override
    {
        RECT client;
        HDC dc;
        int dpiX = 96;
        int dpiY = 96;

        if (extent == nullptr) {
            return E_POINTER;
        }
        GetClientRect(window_, &client);
        dc = GetDC(window_);
        if (dc != nullptr) {
            dpiX = std::max(1, GetDeviceCaps(dc, LOGPIXELSX));
            dpiY = std::max(1, GetDeviceCaps(dc, LOGPIXELSY));
            ReleaseDC(window_, dc);
        }
        extent->cx = MulDiv(std::max<LONG>(1, client.right - client.left),
                            2540, dpiX);
        extent->cy = MulDiv(std::max<LONG>(1, client.bottom - client.top),
                            2540, dpiY);
        return S_OK;
    }

    HRESULT OnTxCharFormatChange(const CHARFORMATW *format) override
    {
        if (format == nullptr || format->cbSize < sizeof(CHARFORMATW)) {
            return E_INVALIDARG;
        }
        std::memset(&characterFormat_, 0, sizeof(characterFormat_));
        std::memcpy(&characterFormat_, format,
                    std::min<SIZE_T>(format->cbSize,
                                     sizeof(characterFormat_)));
        characterFormat_.cbSize = sizeof(characterFormat_);
        return S_OK;
    }

    HRESULT OnTxParaFormatChange(const PARAFORMAT *format) override
    {
        if (format == nullptr || format->cbSize < sizeof(PARAFORMAT)) {
            return E_INVALIDARG;
        }
        std::memset(&paragraphFormat_, 0, sizeof(paragraphFormat_));
        std::memcpy(&paragraphFormat_, format,
                    std::min<SIZE_T>(format->cbSize,
                                     sizeof(paragraphFormat_)));
        paragraphFormat_.cbSize = sizeof(paragraphFormat_);
        return S_OK;
    }

    HRESULT TxGetPropertyBits(DWORD mask, DWORD *bits) override
    {
        if (bits == nullptr) {
            return E_POINTER;
        }
        propertyBits_ = ComputePropertyBits(
            backend_ == RENDER_ENGINE_BACKEND_DIRECTWRITE ||
            (backend_ == RENDER_ENGINE_BACKEND_NONE &&
             d2dFactory_ != nullptr));
        *bits = propertyBits_ & mask;
        return S_OK;
    }

    HRESULT TxNotify(DWORD notification, void *data) override
    {
        HWND parent = GetParent(window_);
        UINT_PTR identifier = static_cast<UINT_PTR>(
            GetWindowLongPtrW(window_, GWLP_ID));
        LRESULT result = 0;

        if (parent == nullptr) {
            return S_FALSE;
        }
        if (notification == EN_CHANGE) {
            formatterDirty_ = TRUE;
        }
        if (notification < EN_MSGFILTER) {
            result = SendMessageW(parent, WM_COMMAND,
                                  MAKEWPARAM(identifier, notification),
                                  reinterpret_cast<LPARAM>(window_));
        } else {
            NMHDR fallback = {};
            NMHDR *header = data != nullptr
                                ? static_cast<NMHDR *>(data)
                                : &fallback;
            NMHDR saved = *header;
            header->hwndFrom = window_;
            header->idFrom = identifier;
            header->code = notification;
            result = SendMessageW(
                parent, WM_NOTIFY, static_cast<WPARAM>(identifier),
                reinterpret_cast<LPARAM>(header));
            *header = saved;
        }
        return result == 0 ? S_OK : S_FALSE;
    }

    HIMC TxImmGetContext() override
    {
        return ImmGetContext(window_);
    }

    void TxImmReleaseContext(HIMC context) override
    {
        if (context != nullptr) {
            ImmReleaseContext(window_, context);
        }
    }

    HRESULT TxGetSelectionBarWidth(LONG *width) override
    {
        if (width == nullptr) {
            return E_POINTER;
        }
        *width = 0;
        return S_OK;
    }

    BOOL TxIsDoubleClickPending() override
    {
        MSG message;
        return PeekMessageW(&message, window_, WM_LBUTTONDBLCLK,
                            WM_LBUTTONDBLCLK, PM_NOREMOVE);
    }

    HRESULT TxGetWindow(HWND *window) override
    {
        if (window == nullptr) {
            return E_POINTER;
        }
        *window = window_;
        return S_OK;
    }

    HRESULT TxSetForegroundWindow() override
    {
        HWND root = GetAncestor(window_, GA_ROOT);
        return SetForegroundWindow(root != nullptr ? root : window_)
                   ? S_OK : S_FALSE;
    }

    HPALETTE TxGetPalette() override
    {
        return nullptr;
    }

    HRESULT TxGetEastAsianFlags(LONG *flags) override
    {
        if (flags == nullptr) {
            return E_POINTER;
        }
        LONG_PTR style = GetWindowLongPtrW(window_, GWL_STYLE);
        *flags = static_cast<LONG>(style) & (ES_SELFIME | ES_NOIME);
        return S_OK;
    }

    HCURSOR TxSetCursor2(HCURSOR cursor, BOOL) override
    {
        return SetCursor(cursor);
    }

    void TxFreeTextServicesNotification() override
    {
        servicesFreed_ = TRUE;
    }

    HRESULT TxGetEditStyle(DWORD item, DWORD *data) override
    {
        if (data == nullptr) {
            return E_POINTER;
        }
        *data = item == TXES_ISDIALOG ? 0 : 0;
        return S_OK;
    }

    HRESULT TxGetWindowStyles(DWORD *style, DWORD *extendedStyle) override
    {
        if (style == nullptr || extendedStyle == nullptr) {
            return E_POINTER;
        }
        *style = static_cast<DWORD>(
            GetWindowLongPtrW(window_, GWL_STYLE));
        *extendedStyle = static_cast<DWORD>(
            GetWindowLongPtrW(window_, GWL_EXSTYLE));
        return S_OK;
    }

    HRESULT TxShowDropCaret(BOOL show, HDC, LPCRECT) override
    {
        return TxShowCaret(show) ? S_OK : S_FALSE;
    }

    HRESULT TxDestroyCaret() override
    {
        return DestroyCaret() ? S_OK : S_FALSE;
    }

    HRESULT TxGetHorzExtent(LONG *extent) override
    {
        RECT client;
        if (extent == nullptr) {
            return E_POINTER;
        }
        GetClientRect(window_, &client);
        *extent = std::max<LONG>(0, client.right - client.left);
        return S_OK;
    }

private:
    ~RenderEditor() = default;

    BOOL EnsureFormatterWindow()
    {
        LRESULT mask;

        if (formatterWindow_ != nullptr &&
            IsWindow(formatterWindow_)) {
            return TRUE;
        }
        formatterWindow_ = CreateWindowExW(
            0, MSFTEDIT_CLASS, L"",
            WS_CHILD | ES_MULTILINE | ES_WANTRETURN | ES_NOHIDESEL,
            0, 0, 1, 1, window_, nullptr, runtime.instance, nullptr);
        if (formatterWindow_ == nullptr) {
            return FALSE;
        }
        SendMessageW(formatterWindow_, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
        mask = SendMessageW(formatterWindow_, EM_GETEVENTMASK, 0, 0);
        SendMessageW(formatterWindow_, EM_SETEVENTMASK, 0,
                     mask & ~(ENM_CHANGE | ENM_UPDATE | ENM_SELCHANGE |
                              ENM_SCROLL | ENM_PAGECHANGE));
        formatterDirty_ = TRUE;
        return TRUE;
    }

    BOOL SynchronizeFormatter()
    {
        std::vector<BYTE> rtf;
        RtfTransfer transfer = {&rtf, 0};
        EDITSTREAM stream = {};
        LRESULT typography;

        if (!EnsureFormatterWindow()) {
            return FALSE;
        }
        if (!formatterDirty_) {
            return TRUE;
        }
        stream.dwCookie = reinterpret_cast<DWORD_PTR>(&transfer);
        stream.pfnCallback = stream_rtf_out;
        if (SendToServices(EM_STREAMOUT, SF_RTF,
                           reinterpret_cast<LPARAM>(&stream), nullptr) !=
                S_OK ||
            stream.dwError != ERROR_SUCCESS || rtf.empty()) {
            return FALSE;
        }

        SendMessageW(formatterWindow_, WM_SETREDRAW, FALSE, 0);
        SetWindowTextW(formatterWindow_, L"");
        transfer.offset = 0;
        std::memset(&stream, 0, sizeof(stream));
        stream.dwCookie = reinterpret_cast<DWORD_PTR>(&transfer);
        stream.pfnCallback = stream_rtf_in;
        SendMessageW(formatterWindow_, EM_STREAMIN, SF_RTF,
                     reinterpret_cast<LPARAM>(&stream));
        typography = 0;
        SendToServices(EM_GETTYPOGRAPHYOPTIONS, 0, 0, &typography);
        SendMessageW(formatterWindow_, EM_SETTYPOGRAPHYOPTIONS,
                     static_cast<WPARAM>(typography),
                     TO_ADVANCEDTYPOGRAPHY | TO_SIMPLELINEBREAK);
        SendMessageW(formatterWindow_, EM_EMPTYUNDOBUFFER, 0, 0);
        SendMessageW(formatterWindow_, EM_SETMODIFY, FALSE, 0);
        SendMessageW(formatterWindow_, WM_SETREDRAW, TRUE, 0);
        if (stream.dwError != ERROR_SUCCESS) {
            return FALSE;
        }
        formatterDirty_ = FALSE;
        return TRUE;
    }

    DWORD ComputePropertyBits(BOOL useD2d) const
    {
        LONG_PTR style = GetWindowLongPtrW(window_, GWL_STYLE);
        /* TXTBIT_ADVANCEDINPUT makes RichEdit register a CoreText input
         * client whose layout callbacks assume an HWND-backed RichEdit
         * implementation.  A windowless host supplies its own focus and
         * message bridge instead; advertising that bit leaves RichEdit's
         * CoreText layout site null and crashes when the control receives
         * focus.  Normal keyboard, IME, pointer and OLE messages continue to
         * flow through TxSendMessage without this optional mode. */
        DWORD bits = TXTBIT_RICHTEXT | TXTBIT_ALLOWBEEP;

        if ((style & ES_MULTILINE) != 0) {
            bits |= TXTBIT_MULTILINE;
        }
        if ((style & ES_READONLY) != 0) {
            bits |= TXTBIT_READONLY;
        }
        if ((style & ES_NOHIDESEL) == 0) {
            bits |= TXTBIT_HIDESELECTION;
        }
        if ((style & ES_SAVESEL) != 0) {
            bits |= TXTBIT_SAVESELECTION;
        }
        if ((style & ES_AUTOHSCROLL) == 0) {
            bits |= TXTBIT_WORDWRAP;
        }
        if ((style & ES_NOOLEDRAGDROP) != 0) {
            bits |= TXTBIT_DISABLEDRAG;
        }
        if (useD2d) {
            bits |= TXTBIT_D2DDWRITE | TXTBIT_D2DSUBPIXELLINES;
        }
        return bits;
    }

    HRESULT EnsureHwndTarget()
    {
        RECT client;
        D2D1_RENDER_TARGET_PROPERTIES properties = {};
        D2D1_HWND_RENDER_TARGET_PROPERTIES windowProperties = {};

        if (hwndTarget_ != nullptr) {
            return S_OK;
        }
        if (d2dFactory_ == nullptr || services2_ == nullptr) {
            return E_NOINTERFACE;
        }
        GetClientRect(window_, &client);
        properties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        properties.pixelFormat.format = DXGI_FORMAT_UNKNOWN;
        properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_UNKNOWN;
        properties.dpiX = 0.0f;
        properties.dpiY = 0.0f;
        properties.usage = D2D1_RENDER_TARGET_USAGE_NONE;
        properties.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
        windowProperties.hwnd = window_;
        windowProperties.pixelSize.width = static_cast<UINT32>(
            std::max<LONG>(1, client.right - client.left));
        windowProperties.pixelSize.height = static_cast<UINT32>(
            std::max<LONG>(1, client.bottom - client.top));
        windowProperties.presentOptions = D2D1_PRESENT_OPTIONS_NONE;
        HRESULT status = d2dFactory_->CreateHwndRenderTarget(
            &properties, &windowProperties, &hwndTarget_);
        if (SUCCEEDED(status) && hwndTarget_ != nullptr) {
            hwndTarget_->SetTextAntialiasMode(
                D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);
            ++targetGeneration_;
        }
        return status;
    }

    HRESULT PaintD2dToWindow(const RECT &)
    {
        RECT client;
        RECTL bounds;
        HRESULT drawStatus;
        HRESULT endStatus;
        HRESULT status = EnsureHwndTarget();

        if (FAILED(status) || hwndTarget_ == nullptr ||
            services2_ == nullptr) {
            return status;
        }
        GetClientRect(window_, &client);
        bounds.left = client.left;
        bounds.top = client.top;
        bounds.right = client.right;
        bounds.bottom = client.bottom;
        D2D1_COLOR_F clear = color_from_colorref(background_);
        hwndTarget_->BeginDraw();
        hwndTarget_->Clear(&clear);
        RECT mutableUpdate = client;
        drawStatus = services2_->TxDrawD2D(
            hwndTarget_, &bounds, &mutableUpdate, TXTVIEW_ACTIVE);
        endStatus = hwndTarget_->EndDraw();
        status = FAILED(drawStatus) ? drawStatus : endStatus;
        if (status == D2DERR_RECREATE_TARGET) {
            DiscardHwndTarget();
        }
        if (SUCCEEDED(status)) {
            ++d2dDrawCount_;
            d2dFailureCount_ = 0;
            lastDrawPath_ = RENDER_DRAW_PATH_DIRECTWRITE;
        }
        lastDrawResult_ = status;
        return status;
    }

    HRESULT PaintD2dToDc(HDC dc, const RECT &client)
    {
        ID2D1DCRenderTarget *target = nullptr;
        D2D1_RENDER_TARGET_PROPERTIES properties = {};
        RECTL bounds;
        RECT update = client;
        HRESULT drawStatus;
        HRESULT endStatus;

        properties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
        properties.dpiX = 0.0f;
        properties.dpiY = 0.0f;
        properties.usage = D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE;
        properties.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
        HRESULT status = d2dFactory_->CreateDCRenderTarget(
            &properties, &target);
        if (FAILED(status) || target == nullptr) {
            lastDrawResult_ = status;
            return status;
        }
        status = target->BindDC(dc, &client);
        if (SUCCEEDED(status)) {
            bounds.left = client.left;
            bounds.top = client.top;
            bounds.right = client.right;
            bounds.bottom = client.bottom;
            D2D1_COLOR_F clear = color_from_colorref(background_);
            target->BeginDraw();
            target->Clear(&clear);
            drawStatus = services2_->TxDrawD2D(
                target, &bounds, &update, TXTVIEW_ACTIVE);
            endStatus = target->EndDraw();
            status = FAILED(drawStatus) ? drawStatus : endStatus;
        }
        target->Release();
        if (SUCCEEDED(status)) {
            ++d2dDrawCount_;
            lastDrawPath_ = RENDER_DRAW_PATH_DIRECTWRITE;
        }
        lastDrawResult_ = status;
        return status;
    }

    HRESULT PaintGdi(HDC dc, const RECT &client, const RECT &update)
    {
        RECTL bounds;
        RECT mutableUpdate = update;
        HRESULT status;

        if (services_ == nullptr || dc == nullptr) {
            return E_INVALIDARG;
        }
        bounds.left = client.left;
        bounds.top = client.top;
        bounds.right = client.right;
        bounds.bottom = client.bottom;
        COLORREF previous = SetBkColor(dc, background_);
        HBRUSH brush = CreateSolidBrush(background_);
        if (brush != nullptr) {
            FillRect(dc, &client, brush);
            DeleteObject(brush);
        }
        status = services_->TxDraw(
            DVASPECT_CONTENT, 0, nullptr, nullptr, dc, nullptr,
            &bounds, nullptr, &mutableUpdate, nullptr, 0,
            TXTVIEW_ACTIVE);
        if (previous != CLR_INVALID) {
            SetBkColor(dc, previous);
        }
        if (SUCCEEDED(status)) {
            ++gdiDrawCount_;
            lastDrawPath_ = RENDER_DRAW_PATH_GDI;
        }
        lastDrawResult_ = status;
        return status;
    }

    void PaintToWindow(HDC dc, const RECT &update)
    {
        RECT client;

        GetClientRect(window_, &client);
        if (IsRectEmpty(&client)) {
            return;
        }
        if (backend_ == RENDER_ENGINE_BACKEND_DIRECTWRITE) {
            HRESULT status = PaintD2dToWindow(update);
            if (SUCCEEDED(status)) {
                return;
            }
            ++d2dFailureCount_;
            if (d2dFailureCount_ >= 3 ||
                status == E_NOINTERFACE || status == E_NOTIMPL) {
                DisableD2d(RENDER_FALLBACK_DRAW_FAILURE);
            } else {
                InvalidateRect(window_, nullptr, FALSE);
                return;
            }
        }
        PaintGdi(dc, client, update);
    }

    void DisableD2d(DWORD reason)
    {
        DWORD mask = TXTBIT_D2DDWRITE | TXTBIT_D2DSIMPLETYPOGRAPHY |
                     TXTBIT_D2DPIXELSNAPPED | TXTBIT_D2DSUBPIXELLINES;
        DiscardHwndTarget();
        propertyBits_ &= ~mask;
        backend_ = RENDER_ENGINE_BACKEND_GDI;
        fallbackReason_ = reason;
        if (services_ != nullptr) {
            services_->OnTxPropertyBitsChange(mask, 0);
        }
        HWND root = GetAncestor(window_, GA_ROOT);
        if (root != nullptr) {
            SendMessageW(root, WCM_RENDERER_CHANGED, 0, 0);
        }
    }

    void DiscardHwndTarget()
    {
        if (hwndTarget_ != nullptr) {
            hwndTarget_->Release();
            hwndTarget_ = nullptr;
        }
    }

    LONG references_;
    HWND window_;
    IUnknown *privateUnknown_;
    ITextServices *services_;
    ITextServices2 *services2_;
    ID2D1Factory *d2dFactory_;
    ID2D1HwndRenderTarget *hwndTarget_;
    HWND formatterWindow_;
    BOOL formatterDirty_;
    CHARFORMAT2W characterFormat_;
    PARAFORMAT2 paragraphFormat_;
    COLORREF background_;
    DWORD propertyBits_;
    DWORD backend_;
    DWORD fallbackReason_;
    ULONGLONG d2dDrawCount_;
    ULONGLONG gdiDrawCount_;
    DWORD lastDrawPath_;
    HRESULT lastDrawResult_;
    UINT targetGeneration_;
    UINT d2dFailureCount_;
    BOOL redrawEnabled_;
    BOOL inPlaceActive_;
    BOOL uiActive_;
    BOOL servicesFreed_;
    LONG activationState_;
    ULONGLONG selectionMessageCount_;
    LONG lastSelectionStart_;
    HRESULT lastSelectionResult_;
    LONG lastSelectionPage_;
};

static RenderEditor *editor_from_window(HWND window)
{
    return reinterpret_cast<RenderEditor *>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
}

static BOOL should_forward_message(UINT message)
{
    if (message >= WM_USER ||
        (message >= EM_GETSEL && message <= EM_CHARFROMPOS) ||
        (message >= 0x0245 && message <= 0x024F)) {
        return TRUE;
    }
    switch (message) {
    case WM_SETTEXT:
    case WM_GETTEXT:
    case WM_GETTEXTLENGTH:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_SETFONT:
    case WM_GETFONT:
    case WM_GETDLGCODE:
    case WM_CONTEXTMENU:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
    case WM_DEADCHAR:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_SYSCHAR:
    case WM_SYSDEADCHAR:
    case WM_UNICHAR:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_MOUSELEAVE:
    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_DROPFILES:
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
    case WM_TIMER:
    case WM_CUT:
    case WM_COPY:
    case WM_PASTE:
    case WM_CLEAR:
    case WM_UNDO:
    case WM_INPUTLANGCHANGE:
    case WM_INPUTLANGCHANGEREQUEST:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_SETCONTEXT:
    case WM_IME_NOTIFY:
    case WM_IME_CONTROL:
    case WM_IME_COMPOSITIONFULL:
    case WM_IME_SELECT:
    case WM_IME_CHAR:
    case WM_IME_REQUEST:
    case WM_IME_KEYDOWN:
    case WM_IME_KEYUP:
    case WM_GETOBJECT:
        return TRUE;
    default:
        return FALSE;
    }
}

static LRESULT CALLBACK render_editor_window_proc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    RenderEditor *editor = editor_from_window(window);

    if (message == WM_NCCREATE) {
        editor = new (std::nothrow) RenderEditor(window);
        if (editor == nullptr) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(editor));
        return TRUE;
    }
    if (message == WM_CREATE) {
        CREATESTRUCTW *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        if (editor == nullptr || !editor->Initialize(create->lpszName)) {
            return -1;
        }
        return 0;
    }
    if (editor == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    switch (message) {
    case WM_PAINT:
        return editor->Paint();
    case WM_PRINTCLIENT:
        return editor->PrintClient(reinterpret_cast<HDC>(wParam));
    case WM_PRINT:
        if ((lParam & PRF_CLIENT) != 0) {
            editor->PrintClient(reinterpret_cast<HDC>(wParam));
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE: {
        LRESULT result = 0;
        editor->Resize();
        HRESULT status = editor->SendToServices(message, wParam, lParam,
                                                &result);
        return status == S_OK ? result
                              : DefWindowProcW(window, message,
                                               wParam, lParam);
    }
    case WM_STYLECHANGED: {
        LRESULT result = DefWindowProcW(window, message, wParam, lParam);
        editor->StyleChanged();
        editor->SendToServices(message, wParam, lParam, nullptr);
        return result;
    }
    case WM_SETREDRAW: {
        LRESULT result = 0;
        editor->SetRedraw(wParam != 0);
        HRESULT status = editor->SendToServices(message, wParam, lParam,
                                                &result);
        return status == S_OK ? result : 0;
    }
    case EM_FORMATRANGE: {
        LRESULT result = 0;
        HRESULT status = editor->SendFormatRange(wParam, lParam, &result);
        return status == S_OK ? result : 0;
    }
    case EM_SETSEL: {
        LRESULT result = 0;
        HRESULT status = editor->SendToServices(message, wParam, lParam,
                                                &result);
        editor->NoteSelectionMessage(static_cast<LONG>(wParam), status);
        return status == S_OK ? result : 0;
    }
    case EM_SETBKGNDCOLOR: {
        LRESULT result = 0;
        HRESULT status = editor->SendToServices(message, wParam, lParam,
                                                &result);
        if (status == S_OK) {
            editor->SetBackground(wParam, lParam);
            InvalidateRect(window, nullptr, TRUE);
            return result;
        }
        break;
    }
    case WM_SETFOCUS: {
        LRESULT result = 0;
        editor->ActivateUi();
        HRESULT status = editor->SendToServices(message, 0, lParam,
                                                &result);
        return status == S_OK ? result
                              : DefWindowProcW(window, message,
                                               wParam, lParam);
    }
    case WM_KILLFOCUS: {
        LRESULT result = 0;
        HRESULT status = editor->SendToServices(message, 0, lParam,
                                                &result);
        editor->DeactivateUi();
        return status == S_OK ? result
                              : DefWindowProcW(window, message,
                                               wParam, lParam);
    }
    case WM_NCDESTROY: {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        editor->Shutdown();
        LRESULT result = DefWindowProcW(window, message, wParam, lParam);
        editor->Release();
        return result;
    }
    default:
        break;
    }

    if (should_forward_message(message)) {
        LRESULT result = 0;
        HRESULT status = editor->SendToServices(message, wParam, lParam,
                                                &result);
        if (status == S_OK) {
            return result;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

extern "C" BOOL render_editor_register(HINSTANCE instance,
                                        HMODULE richEditModule)
{
    WNDCLASSEXW windowClass;

    if (instance == nullptr || richEditModule == nullptr) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (runtime.registered) {
        return runtime.instance == instance &&
               runtime.richEditModule == richEditModule;
    }

    runtime.instance = instance;
    runtime.richEditModule = richEditModule;
    runtime.createTextServices = reinterpret_cast<CreateTextServicesFn>(
        reinterpret_cast<void *>(GetProcAddress(richEditModule,
                                                "CreateTextServices")));
    runtime.shutdownTextServices = reinterpret_cast<ShutdownTextServicesFn>(
        reinterpret_cast<void *>(GetProcAddress(richEditModule,
                                                "ShutdownTextServices")));
    if (runtime.createTextServices == nullptr ||
        !copy_exported_iid(richEditModule, "IID_ITextServices",
                           &runtime.iidTextServices) ||
        !copy_exported_iid(richEditModule, "IID_ITextHost",
                           &runtime.iidTextHost)) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    runtime.hasTextServices2 =
        copy_exported_iid(richEditModule, "IID_ITextServices2",
                          &runtime.iidTextServices2) &&
        copy_exported_iid(richEditModule, "IID_ITextHost2",
                          &runtime.iidTextHost2);

    std::memset(&windowClass, 0, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = render_editor_window_proc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = WORDCRAFT_RENDER_EDITOR_CLASS;
    if (RegisterClassExW(&windowClass) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return FALSE;
    }
    runtime.registered = TRUE;
    return TRUE;
}

extern "C" LRESULT render_editor_query_state(HWND editor, UINT query)
{
    RenderEditor *state;

    if (!render_editor_is_window(editor)) {
        return 0;
    }
    state = editor_from_window(editor);
    return state != nullptr ? state->Query(query) : 0;
}

extern "C" BOOL render_editor_is_window(HWND editor)
{
    WCHAR className[64];

    if (editor == nullptr || !IsWindow(editor) ||
        GetClassNameW(editor, className, ARRAYSIZE(className)) <= 0) {
        return FALSE;
    }
    return lstrcmpW(className, WORDCRAFT_RENDER_EDITOR_CLASS) == 0;
}
